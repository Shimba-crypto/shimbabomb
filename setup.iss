[Setup]
AppName=ShimbaBomb
AppVersion=1.12.0
AppPublisher=Shimba-crypto
DefaultDirName={autopf}\ShimbaBomb
DefaultGroupName=ShimbaBomb
OutputDir=Output
OutputBaseFilename=ShimbaBomb-1.12.0-setup
Compression=lzma2
SolidCompression=yes
SetupIconFile=assets\logo.ico
UninstallDisplayIcon={app}\sb.exe

[Files]
Source: "Output\sb.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\SB REPL"; Filename: "{app}\sb.exe"
Name: "{group}\Uninstall"; Filename: "{uninstallexe}"

[Code]
// Add {app} to user PATH so 'sb' works from any terminal
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if CurStep = ssPostInstall then
  begin
    Exec('cmd.exe', '/c setx PATH "' + ExpandConstant('{app}') + ';%PATH%"',
         '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  end;
end;
