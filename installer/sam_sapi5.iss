; Inno Setup script for the SAM (Software Automatic Mouth) SAPI5 voice.
;
; Installs both the 32-bit and 64-bit SAPI5 engines, all six voices, the
; pronunciation dictionary, and the settings utility.
;
; Accessibility: this uses Inno Setup's standard wizard pages, which are plain
; Win32 controls that screen readers read correctly. Every custom control below
; is given a caption and placed next to its label. Nothing relies on colour,
; images, or mouse-only interaction, and the summary page spells out exactly
; what will be installed.

#define AppName "SAM Voice for SAPI5"
#define AppShortName "SAM Voice"
#define AppVersion "1.0.0"
#define AppPublisher "Software Automatic Mouth"
#define SapiDll "SamVoiceSAPI.dll"
#define SettingsExe "SamVoiceSettings.exe"

[Setup]
AppId={{9C4B1F72-1E4D-4E36-9E0B-0B6F3E2A77D1}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppShortName}
DefaultGroupName={#AppShortName}
OutputBaseFilename=SamVoiceSAPI5_Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

; The SAPI engines register under HKLM, so setup needs administrator rights.
PrivilegesRequired=admin

; 32-bit SAPI applications on 64-bit Windows need the 32-bit engine, so both
; are installed. 64-bit install mode makes {sys} mean the real System32.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; Detailed installer logging, as requested. The log is copied next to the
; program at the end of installation so it is easy to find and attach.
SetupLogging=yes

UninstallDisplayName={#AppName}
UninstallDisplayIcon={app}\{#SettingsExe}
DisableWelcomePage=no
DisableProgramGroupPage=yes
ShowLanguageDialog=no
AllowCancelDuringInstall=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut for SAM Voice Settings"; \
    GroupDescription: "Shortcuts:"
Name: "quicklaunchicon"; Description: "Create a &Start menu shortcut"; \
    GroupDescription: "Shortcuts:"

[Files]
; --- 64-bit engine and the settings utility --------------------------------
Source: "..\build_x64\bin\Release\{#SapiDll}"; DestDir: "{app}"; \
    Flags: ignoreversion
Source: "..\build_x64\bin\Release\{#SettingsExe}"; DestDir: "{app}"; \
    Flags: ignoreversion
Source: "..\build_x64\bin\Release\sam_render.exe"; DestDir: "{app}"; \
    Flags: ignoreversion skipifsourcedoesntexist

; --- 32-bit engine, for 32-bit SAPI applications ---------------------------
Source: "..\build_x86\bin\Release\{#SapiDll}"; DestDir: "{app}\x86"; \
    Flags: ignoreversion

; --- data shared by both architectures -------------------------------------
; The engine looks for cmudict.txt beside the DLL and then one level up, so a
; single copy in {app} serves both {app} and {app}\x86.
Source: "..\bin\cmudict.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "readme.txt"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\SAM Voice Settings"; Filename: "{app}\{#SettingsExe}"; \
    Comment: "Adjust the SAM voices: pitch, speed, mouth, throat, inflection and volume"; \
    Tasks: quicklaunchicon
Name: "{group}\Uninstall {#AppShortName}"; Filename: "{uninstallexe}"; \
    Tasks: quicklaunchicon
Name: "{autodesktop}\SAM Voice Settings"; Filename: "{app}\{#SettingsExe}"; \
    Comment: "Adjust the SAM voices: pitch, speed, mouth, throat, inflection and volume"; \
    Tasks: desktopicon

[Run]
Filename: "{app}\{#SettingsExe}"; \
    Description: "Open SAM &Voice Settings now"; \
    Flags: postinstall nowait skipifsilent

[UninstallDelete]
; The registration log we write during install.
Type: files; Name: "{app}\install-log.txt"
Type: dirifempty; Name: "{app}\x86"
Type: dirifempty; Name: "{app}"

[Code]
var
  ResultPage: TOutputMsgWizardPage;

{ Registers or unregisters one DLL with the matching regsvr32 for its
  architecture, and reports the outcome into the setup log. }
function RunRegSvr(const RegSvrPath, DllPath: String; const Unregister: Boolean): Boolean;
var
  Args: String;
  ResultCode: Integer;
begin
  if Unregister then
    Args := '/s /u "' + DllPath + '"'
  else
    Args := '/s "' + DllPath + '"';

  Log('Running: ' + RegSvrPath + ' ' + Args);
  Result := Exec(RegSvrPath, Args, '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  if Result then
  begin
    Result := (ResultCode = 0);
    Log('  exit code ' + IntToStr(ResultCode));
  end
  else
    Log('  could not start regsvr32, error ' + IntToStr(ResultCode));
end;

procedure InitializeWizard();
begin
  { A plain text page at the end, so a screen reader user gets a clear
    spoken confirmation of what happened rather than only an icon. }
  ResultPage := CreateOutputMsgPage(wpInstalling,
    'Registration results',
    'What setup registered on this computer',
    'Setup is registering the SAM speech engines.');
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Summary: String;
  Ok64, Ok32: Boolean;
  Dll64, Dll32: String;
begin
  if CurStep = ssPostInstall then
  begin
    Dll64 := ExpandConstant('{app}\{#SapiDll}');
    Dll32 := ExpandConstant('{app}\x86\{#SapiDll}');

    { 64-bit engine with the 64-bit regsvr32 in System32. }
    Ok64 := RunRegSvr(ExpandConstant('{sys}\regsvr32.exe'), Dll64, False);

    { 32-bit engine with the 32-bit regsvr32 in SysWOW64, so that 32-bit
      SAPI applications can see the voices too. }
    Ok32 := RunRegSvr(ExpandConstant('{syswow64}\regsvr32.exe'), Dll32, False);

    if Ok64 then
      Summary := 'The 64-bit SAM speech engine was registered successfully.'
    else
      Summary := 'The 64-bit SAM speech engine could NOT be registered.';

    Summary := Summary + #13#10;
    if Ok32 then
      Summary := Summary + 'The 32-bit SAM speech engine was registered successfully.'
    else
      Summary := Summary + 'The 32-bit SAM speech engine could NOT be registered.';

    { Note: a line here must never begin with '#', or the preprocessor reads
      it as a directive -- keep the CRLF constants at the end of a line. }
    Summary := Summary + #13#10#13#10 +
      'Six SAM voices are now available to any program that uses Windows speech: ' +
      'Sam, Elf, Little Robot, Stuffy Guy, Little Old Lady and Extra Terrestrial.' +
      '' + #13#10#13#10 +
      'To change pitch, speed, mouth, throat, inflection or volume, open ' +
      'SAM Voice Settings from the desktop or the Start menu. Changes take ' +
      'effect on the next thing spoken.';

    if (not Ok64) or (not Ok32) then
      Summary := Summary + #13#10#13#10 +
        'A registration step failed. The setup log has the details; it is ' +
        'saved as install-log.txt in the installation folder.';

    ResultPage.MsgLabel.Caption := Summary;

    { Keep a copy of the setup log next to the program, so it is easy to find. }
    CopyFile(ExpandConstant('{log}'), ExpandConstant('{app}\install-log.txt'), False);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then
  begin
    { Unregister before the files are deleted. }
    RunRegSvr(ExpandConstant('{sys}\regsvr32.exe'),
              ExpandConstant('{app}\{#SapiDll}'), True);
    RunRegSvr(ExpandConstant('{syswow64}\regsvr32.exe'),
              ExpandConstant('{app}\x86\{#SapiDll}'), True);

    { Give any host that still has the DLL loaded a moment to let go. }
    Sleep(1500);
  end;
end;
