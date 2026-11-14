; Inno Setup script (Inno Setup 6.3 or newer, or 7) - built by build-installer.ps1
; The installer carries ONLY: this project's source, a compiler and Python. No ROM and no code translated from the ROM.
; On the user's PC it asks for the arcade ROM, builds the game from it, and installs the result.

#ifndef AppVersion
  #define AppVersion "0.9.34"
#endif
#ifndef StageDir
  #define StageDir "stage"
#endif

[Setup]
AppId={{B8E71608-A56B-47A7-8C53-DF463774ACD0}
AppName=Super Hang-On Ultimate
AppVersion={#AppVersion}
AppPublisher=chaotix359
DefaultDirName={autopf}\Super Hang-On Ultimate
DefaultGroupName=Super Hang-On Ultimate
DisableProgramGroupPage=yes
; per-user install: the game saves its settings and high scores next to the .exe, so the folder must be writable
PrivilegesRequired=lowest
MinVersion=10.0
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
InfoBeforeFile=about.txt
SetupIconFile={#StageDir}\src\source\res\shangon.ico
UninstallDisplayIcon={app}\SuperHangOn.exe
OutputDir=output
OutputBaseFilename=SuperHangOnUltimate-Setup-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ExtraDiskSpaceRequired=1200000000

[Messages]
; (a message, not a [Setup] directive)
DiskSpaceMBLabel=At least [mb] MB of free disk space is required (most of it is temporary).

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; Flags: unchecked

[Files]
; --- installed to the game folder ---
Source: "{#StageDir}\src\README.txt";        DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\src\LICENSE";           DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\src\NOTICE.md";         DestDir: "{app}"; Flags: ignoreversion
Source: "{#StageDir}\src\licenses\*";        DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs createallsubdirs
; --- build tools, used during installation only (extracted to Setup's temporary folder, removed when Setup exits) ---
Source: "{#StageDir}\src\*";          DestDir: "{tmp}\build\src";        Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\python\*";       DestDir: "{tmp}\build\python";     Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#StageDir}\llvm-mingw\*";   DestDir: "{tmp}\build\llvm-mingw"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "build-game.cmd";             DestDir: "{tmp}\build";            Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Super Hang-On Ultimate"; Filename: "{app}\SuperHangOn.exe"; WorkingDir: "{app}"
Name: "{autodesktop}\Super Hang-On Ultimate";  Filename: "{app}\SuperHangOn.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\SuperHangOn.exe"; Description: "Start Super Hang-On Ultimate"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
; the .exe is made during installation, so Setup does not know it; settings, high scores, saves and your ROM copies are kept
Type: files; Name: "{app}\SuperHangOn.exe"
Type: files; Name: "{app}\install-build.log"

[Code]
var
  RomPage: TInputFileWizardPage;
  MdPage: TInputFileWizardPage;

procedure InitializeWizard;
begin
  RomPage := CreateInputFilePage(wpSelectDir,
    'Arcade ROM',
    'Select your Super Hang-On arcade ROM set',
    'The game is built on this computer from your own copy of the MAME ROM set "shangon.zip" (Super Hang-On, unprotected). It is not included with this installer.');
  RomPage.Add('Arcade ROM set (shangon.zip):', 'Zip files (*.zip)|*.zip|All files (*.*)|*.*', '.zip');

  MdPage := CreateInputFilePage(RomPage.ID,
    'Mega Drive ROM (optional)',
    'Select your Mega Drive ROM for Original Mode',
    'Optional: "Super Hang-On (World) (En,Ja) (Rev A)". It enables the Mega Drive Original Mode. You can leave this empty and put the file in the game folder later.');
  MdPage.Add('Mega Drive ROM:', 'Mega Drive ROMs|*.md;*.gen;*.bin;*.zip|All files (*.*)|*.*', '.md');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = RomPage.ID then
  begin
    if not FileExists(RomPage.Values[0]) then
    begin
      MsgBox('Please choose your arcade ROM set (shangon.zip). The game cannot be built without it.', mbError, MB_OK);
      Result := False;
    end;
  end;
  if CurPageID = MdPage.ID then
  begin
    if (MdPage.Values[0] <> '') and not FileExists(MdPage.Values[0]) then
    begin
      MsgBox('That Mega Drive ROM file was not found. Choose another file or leave the box empty.', mbError, MB_OK);
      Result := False;
    end;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
  AppDir, TmpLog, Params: String;
begin
  if CurStep <> ssPostInstall then Exit;

  AppDir := ExpandConstant('{app}');
  TmpLog := GetEnv('TEMP') + '\SuperHangOn-install.log';

  { put the user's ROM(s) in the game folder, where the game looks for them }
  if not FileCopy(RomPage.Values[0], AppDir + '\shangon.zip', False) then
    RaiseException('Could not copy the arcade ROM into ' + AppDir + '.');
  if MdPage.Values[0] <> '' then
    FileCopy(MdPage.Values[0], AppDir + '\' + ExtractFileName(MdPage.Values[0]), False);

  WizardForm.StatusLabel.Caption := 'Building the game from your ROM. This takes several minutes - please wait...';
  WizardForm.FilenameLabel.Caption := 'Details are written to ' + TmpLog;
  WizardForm.ProgressGauge.Style := npbstMarquee;

  Params := '/C ""' + ExpandConstant('{tmp}\build\build-game.cmd') + '" "' + ExpandConstant('{tmp}\build') + '" "' + AppDir + '" > "' + TmpLog + '" 2>&1"';
  if not Exec(ExpandConstant('{cmd}'), Params, '', SW_HIDE, ewWaitUntilTerminated, ResultCode) or (ResultCode <> 0) then
  begin
    WizardForm.ProgressGauge.Style := npbstNormal;
    RaiseException('The game could not be built (error ' + IntToStr(ResultCode) + ').' + #13#10#13#10 +
      'Possible causes: a ROM set that is not the unprotected MAME "shangon" set, or a build error.' + #13#10 +
      'Details: ' + TmpLog);
  end;

  WizardForm.ProgressGauge.Style := npbstNormal;
  FileCopy(TmpLog, AppDir + '\install-build.log', False);
end;
