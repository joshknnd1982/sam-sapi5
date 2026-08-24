// The formant synthesizer. Turns timed phonemes into 8-bit unsigned PCM by
// summing two sine formants and one rectangular formant, resetting their phase
// on every glottal pulse.
#include "sam_engine.hpp"
#include "sam_tables.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace sam {
namespace {

using namespace tables;

constexpr int PHONEME_PERIOD = 1;
constexpr int PHONEME_QUESTION = 2;
constexpr int RISING_INFLECTION = 255;
constexpr int FALLING_INFLECTION = 1;

// Sample-timing table, indexed by [previous write kind][current write kind].
const int TIMETABLE[5][5] = {
    {162, 167, 167, 127, 128},  // formant synthesis
    {226, 60, 60, 0, 0},        // unvoiced sample 0
    {225, 60, 59, 0, 0},        // unvoiced sample 1
    {200, 0, 0, 54, 55},        // voiced sample 0
    {199, 0, 0, 54, 54},        // voiced sample 1
};

// Grows on demand rather than pre-allocating the original's very loose upper
// bound; the written bytes are identical either way.
class OutputBuffer
{
public:
    void write(int index, int value)
    {
        const int scaled = (value & 15) * 16;
        const int a[5] = {scaled, scaled, scaled, scaled, scaled};
        write_array(index, a);
    }

    void write_array(int index, const int (&values)[5])
    {
        bufferpos_ += TIMETABLE[old_timetable_index_][index];
        const std::size_t pos = static_cast<std::size_t>(bufferpos_ / 50);
        old_timetable_index_ = index;

        if (pos + 5 > buffer_.size()) {
            buffer_.resize(pos + 5, 0);
        }
        for (int k = 0; k < 5; ++k) {
            buffer_[pos + static_cast<std::size_t>(k)] =
                static_cast<std::uint8_t>(values[k]);
        }
    }

    std::vector<std::uint8_t> take()
    {
        // The original returns buffer[:bufferpos // 50] from a pre-sized buffer.
        buffer_.resize(static_cast<std::size_t>(bufferpos_ / 50), 0);
        return std::move(buffer_);
    }

private:
    std::vector<std::uint8_t> buffer_;
    long long bufferpos_ = 0;
    int old_timetable_index_ = 0;
};

// Per-phoneme formant frequencies, scaled by the mouth and throat settings.
struct FreqData
{
    int f[3][80];
};

FreqData set_mouth_throat(int mouth, int throat)
{
    auto trans = [](int factor, int initial) {
        return (((factor * initial) >> 8) & 0xFF) << 1;
    };

    FreqData d{};
    for (int i = 0; i < FREQ_AMP_COUNT; ++i) {
        d.f[0][i] = static_cast<int>(FREQUENCY_DATA[i] & 0xFF);
        d.f[1][i] = static_cast<int>((FREQUENCY_DATA[i] >> 8) & 0xFF);
        d.f[2][i] = static_cast<int>((FREQUENCY_DATA[i] >> 16) & 0xFF);
    }
    // Only vowels, diphthongs and sonorants are reshaped.
    for (int pos = 5; pos < 30; ++pos) {
        d.f[0][pos] = trans(mouth, d.f[0][pos]);
        d.f[1][pos] = trans(throat, d.f[1][pos]);
    }
    for (int pos = 48; pos < 54; ++pos) {
        d.f[0][pos] = trans(mouth, d.f[0][pos]);
        d.f[1][pos] = trans(throat, d.f[1][pos]);
    }
    return d;
}

struct Frames
{
    std::vector<int> pitches;
    std::vector<int> frequency[3];
    std::vector<int> amplitude[3];
    std::vector<int> sampled_consonant_flag;
};

// Bends the pitch contour up or down over the 30 frames before `pos`.
void add_inflection(int inflection, long pos, std::vector<int>& pitches)
{
    const long end = pos;
    pos = (pos < 30) ? 0 : pos - 30;

    const long n = static_cast<long>(pitches.size());
    while (pos < n && pitches[static_cast<std::size_t>(pos)] == 127) {
        ++pos;
    }
    while (pos != end && pos < n) {
        int a = pitches[static_cast<std::size_t>(pos)];
        a += inflection;
        pitches[static_cast<std::size_t>(pos)] = a & 0xFF;
        ++pos;
        while (pos != end && pos < n && pitches[static_cast<std::size_t>(pos)] == 255) {
            ++pos;
        }
    }
}

Frames create_frames(int pitch, const std::vector<Phoneme>& tuples,
                     const FreqData& fd, int inflection_amount)
{
    const double scale = inflection_amount / 50.0;
    int rising = (scale > 0) ? static_cast<int>(RISING_INFLECTION * scale) : 0;
    int falling = (scale > 0) ? static_cast<int>(FALLING_INFLECTION * scale) : 0;
    rising = std::max(0, std::min(255, rising));
    falling = std::max(0, std::min(255, falling));

    Frames fr;
    long x = 0;

    for (const Phoneme& t : tuples) {
        const int phoneme = t.index;
        if (phoneme == PHONEME_PERIOD) {
            add_inflection(falling, x, fr.pitches);
        } else if (phoneme == PHONEME_QUESTION) {
            add_inflection(rising, x, fr.pitches);
        }

        int phase1 = (t.stress >= 0 && t.stress < STRESS_PITCH_COUNT)
                         ? STRESS_PITCH_TABLE[t.stress]
                         : 0;
        phase1 = static_cast<int>(phase1 * scale);

        const int in_range = (phoneme >= 0 && phoneme < FREQ_AMP_COUNT);
        const std::uint32_t amp = in_range ? AMPLITUDE_DATA[phoneme] : 0;
        const int scf = in_range ? SAMPLED_CONSONANT_FLAGS[phoneme] : 0;

        for (int k = 0; k < t.length; ++k) {
            fr.frequency[0].push_back(in_range ? fd.f[0][phoneme] : 0);
            fr.frequency[1].push_back(in_range ? fd.f[1][phoneme] : 0);
            fr.frequency[2].push_back(in_range ? fd.f[2][phoneme] : 0);
            fr.amplitude[0].push_back(static_cast<int>(amp & 0xFF));
            fr.amplitude[1].push_back(static_cast<int>((amp >> 8) & 0xFF));
            fr.amplitude[2].push_back(static_cast<int>((amp >> 16) & 0xFF));
            fr.sampled_consonant_flag.push_back(scf);
            fr.pitches.push_back((pitch + phase1) & 0xFF);
            ++x;
        }
    }
    return fr;
}

// Linearly ramps each parameter across the boundary between two phonemes.
long create_transitions(Frames& fr, const std::vector<Phoneme>& tuples)
{
    std::vector<int>* tables[7] = {
        &fr.pitches,      &fr.frequency[0], &fr.frequency[1], &fr.frequency[2],
        &fr.amplitude[0], &fr.amplitude[1], &fr.amplitude[2],
    };

    auto read = [&](int table, long pos) -> int {
        const std::vector<int>& v = *tables[table];
        if (pos < 0 || pos >= static_cast<long>(v.size())) {
            return 0;
        }
        return v[static_cast<std::size_t>(pos)];
    };

    auto interpolate = [&](int width, int table, long frame, int change) {
        if (width == 0) {
            return;
        }
        const bool sign = change < 0;
        const int remainder = std::abs(change) % width;
        const int div = change / width;  // truncates toward zero, as Python's int()
        int error = 0;
        int pos = width;

        std::vector<int>& v = *tables[table];
        while (pos > 1) {
            --pos;
            int val = read(table, frame) + div;
            error += remainder;
            if (error >= width) {
                error -= width;
                if (sign) {
                    --val;
                } else if (val) {
                    ++val;
                }
            }
            ++frame;
            if (frame < static_cast<long>(v.size())) {
                v[static_cast<std::size_t>(frame)] = val;
            }
        }
    };

    long boundary = 0;
    for (std::size_t pos = 0; pos + 1 < tuples.size(); ++pos) {
        const int phoneme = tuples[pos].index;
        const int next_phoneme = tuples[pos + 1].index;

        const int next_rank = (next_phoneme < BLEND_COUNT) ? BLEND_RANK[next_phoneme] : 0;
        const int rank = (phoneme < BLEND_COUNT) ? BLEND_RANK[phoneme] : 0;

        int out_blend_frames, in_blend_frames;
        if (rank == next_rank) {
            out_blend_frames = (phoneme < BLEND_COUNT) ? OUT_BLEND_LENGTH[phoneme] : 0;
            in_blend_frames = (next_phoneme < BLEND_COUNT) ? OUT_BLEND_LENGTH[next_phoneme] : 0;
        } else if (rank < next_rank) {
            out_blend_frames = (next_phoneme < BLEND_COUNT) ? IN_BLEND_LENGTH[next_phoneme] : 0;
            in_blend_frames = (next_phoneme < BLEND_COUNT) ? OUT_BLEND_LENGTH[next_phoneme] : 0;
        } else {
            out_blend_frames = (phoneme < BLEND_COUNT) ? OUT_BLEND_LENGTH[phoneme] : 0;
            in_blend_frames = (phoneme < BLEND_COUNT) ? IN_BLEND_LENGTH[phoneme] : 0;
        }

        boundary += tuples[pos].length;
        const long trans_end = boundary + in_blend_frames;
        const long trans_start = boundary - out_blend_frames;
        const int trans_length = out_blend_frames + in_blend_frames;

        if (((trans_length - 2) & 128) == 0) {
            const int cur_width = tuples[pos].length >> 1;
            const int next_width = tuples[pos + 1].length >> 1;
            const long pitch_end = boundary + next_width;
            const long pitch_start = boundary - cur_width;

            if (pitch_end < static_cast<long>(fr.pitches.size()) && pitch_start >= 0) {
                const int pitch_diff = fr.pitches[static_cast<std::size_t>(pitch_end)] -
                                       fr.pitches[static_cast<std::size_t>(pitch_start)];
                interpolate(cur_width + next_width, 0, trans_start, pitch_diff);
            }
            for (int table = 1; table < 7; ++table) {
                const int value = read(table, trans_end) - read(table, trans_start);
                interpolate(trans_length, table, trans_start, value);
            }
        }
    }

    if (!tuples.empty()) {
        return boundary + tuples.back().length;
    }
    return boundary;
}

Frames prepare_frames(const std::vector<Phoneme>& phonemes, int pitch, int mouth,
                      int throat, bool singmode, int inflection, long& frame_count)
{
    const FreqData fd = set_mouth_throat(mouth, throat);
    Frames fr = create_frames(pitch, phonemes, fd, inflection);
    frame_count = create_transitions(fr, phonemes);

    if (!singmode) {
        const double scale = inflection / 50.0;
        for (std::size_t i = 0; i < fr.pitches.size(); ++i) {
            if (i < fr.frequency[0].size()) {
                const int f1_adjust = static_cast<int>((fr.frequency[0][i] >> 1) * scale);
                fr.pitches[i] = (fr.pitches[i] - f1_adjust) & 0xFF;
            }
        }
    }

    for (std::size_t j = 0; j < 3; ++j) {
        for (std::size_t i = 0; i < fr.amplitude[j].size(); ++i) {
            const int a = fr.amplitude[j][i];
            if (a >= 0 && a < AMPLITUDE_RESCALE_COUNT) {
                fr.amplitude[j][i] = AMPLITUDE_RESCALE[a];
            }
        }
    }
    return fr;
}

// Plays back one of the digitised consonant samples.
int render_sample(OutputBuffer& out, int last_sample_offset, int consonant_flag, int pitch)
{
    const int kind = (consonant_flag & 7) - 1;
    const int sample_page = (kind * 256) & 0xFFFF;
    int off = consonant_flag & 248;

    auto render_inner = [&](int index1, int value1, int index0, int value0) {
        const int sample_idx = sample_page + off;
        if (sample_idx < 0 || sample_idx >= SAMPLE_TABLE_COUNT) {
            return;
        }
        int sample = SAMPLE_TABLE[sample_idx];
        for (int bit = 8; bit > 0; --bit) {
            if (sample & 128) {
                out.write(index1, value1);
            } else {
                out.write(index0, value0);
            }
            sample = (sample << 1) & 0xFF;
        }
    };

    if (off == 0) {
        int phase1 = ((pitch >> 4) ^ 255) & 0xFF;
        off = last_sample_offset & 0xFF;
        for (;;) {
            render_inner(3, 26, 4, 6);
            off = (off + 1) & 0xFF;
            phase1 = (phase1 + 1) & 0xFF;
            if (phase1 == 0) {
                break;
            }
        }
        return off;
    }

    off = (off ^ 255) & 0xFF;
    // Python indexes with a possibly-negative `kind`, which wraps to the end.
    int vi = kind;
    if (vi < 0) {
        vi += SAMPLED_CONSONANT_VALUES0_COUNT;
    }
    const int value0 = (vi >= 0 && vi < SAMPLED_CONSONANT_VALUES0_COUNT)
                           ? (SAMPLED_CONSONANT_VALUES0[vi] & 0xFF)
                           : 0;
    for (;;) {
        render_inner(2, 5, 1, value0);
        off = (off + 1) & 0xFF;
        if (off == 0) {
            break;
        }
    }
    return last_sample_offset;
}

void process_frames(OutputBuffer& out, long frame_count, int speed, const Frames& fr)
{
    if (fr.pitches.empty()) {
        return;
    }

    auto at = [](const std::vector<int>& v, std::size_t i) -> int {
        return i < v.size() ? v[i] : 0;
    };

    int speedcounter = speed;
    long long phase1 = 0, phase2 = 0, phase3 = 0;
    int last_sample_offset = 0;
    std::size_t pos = 0;

    int glottal_pulse = fr.pitches[0];
    int mem38 = glottal_pulse * 3 / 4;  // int(glottal_pulse * 0.75)

    while (frame_count > 0) {
        if (pos >= fr.sampled_consonant_flag.size()) {
            break;
        }
        const int flags = fr.sampled_consonant_flag[pos];

        if ((flags & 248) != 0) {
            const int pitch_val = at(fr.pitches, pos & 0xFF);
            last_sample_offset = render_sample(out, last_sample_offset, flags, pitch_val);
            pos += 2;
            frame_count -= 2;
            speedcounter = speed;
            continue;
        }

        int ary[5];
        long long p1 = phase1 * 256;
        long long p2 = phase2 * 256;
        long long p3 = phase3 * 256;

        for (int k = 0; k < 5; ++k) {
            const int sp1 = SINUS[(p1 >> 8) & 0xFF];
            const int sp2 = SINUS[(p2 >> 8) & 0xFF];
            const int rp3 = (((p3 >> 8) & 0xFF) < 129) ? -0x70 : 0x70;

            const int amp0 = at(fr.amplitude[0], pos) & 0x0F;
            const int amp1 = at(fr.amplitude[1], pos) & 0x0F;
            const int amp2 = at(fr.amplitude[2], pos) & 0x0F;

            const int mux = (sp1 * amp0 + sp2 * amp1 + rp3 * amp2) / 32 + 128;
            ary[k] = std::max(0, std::min(255, mux));

            p1 += static_cast<long long>(at(fr.frequency[0], pos)) * 64;
            p2 += static_cast<long long>(at(fr.frequency[1], pos)) * 64;
            p3 += static_cast<long long>(at(fr.frequency[2], pos)) * 64;
        }
        out.write_array(0, ary);

        --speedcounter;
        if (speedcounter == 0) {
            ++pos;
            --frame_count;
            if (frame_count == 0) {
                return;
            }
            speedcounter = speed;
        }

        --glottal_pulse;
        if (glottal_pulse != 0) {
            --mem38;
            if (mem38 != 0 || flags == 0) {
                phase1 += at(fr.frequency[0], pos);
                phase2 += at(fr.frequency[1], pos);
                phase3 += at(fr.frequency[2], pos);
                continue;
            }
            const int pitch_val = at(fr.pitches, pos & 0xFF);
            last_sample_offset = render_sample(out, last_sample_offset, flags, pitch_val);
        }

        glottal_pulse = (pos < fr.pitches.size()) ? fr.pitches[pos] : 0;
        mem38 = glottal_pulse * 3 / 4;
        phase1 = phase2 = phase3 = 0;
    }
}

}  // namespace

std::vector<std::uint8_t> render(const std::vector<Phoneme>& phonemes,
                                 const VoiceParams& params)
{
    const int pitch = params.pitch & 0xFF;
    const int mouth = params.mouth & 0xFF;
    const int throat = params.throat & 0xFF;
    const int speed = (params.speed ? params.speed : 72) & 0xFF;

    long frame_count = 0;
    Frames fr = prepare_frames(phonemes, pitch, mouth, throat, params.singmode,
                               params.inflection, frame_count);

    long total_frames = 0;
    for (const Phoneme& p : phonemes) {
        total_frames += p.length;
    }
    if (total_frames == 0 || speed == 0) {
        return {};
    }

    OutputBuffer out;
    process_frames(out, frame_count, speed, fr);
    return out.take();
}

}  // namespace sam
