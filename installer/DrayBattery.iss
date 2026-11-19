#define AppName "DrayBattery"
#define AppVersion "0.1.11"
#define AppExe "DrayBattery.exe"

[Setup]
AppId={{E3BF8E9C-46B5-47F7-9079-56D39E020DB1}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=DrayBattery Contributors
AppPublisherURL=https://github.com/deepsky303/DrayBattery
AppSupportURL=https://github.com/deepsky303/DrayBattery/issues
AppUpdatesURL=https://github.com/deepsky303/DrayBattery/releases
DefaultDirName={localappdata}\Programs\DrayBattery
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
UninstallDisplayIcon={app}\{#AppExe}
LicenseFile=..\LICENSE
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
SetupIconFile=..\res\app.ico
OutputDir=..\dist
OutputBaseFilename=DrayBattery-{#AppVersion}-setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no
ChangesAssociations=no
VersionInfoVersion=0.1.11.0

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\dist\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.ru.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\CHANGELOG.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\docs\*.png"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\docs\*.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "..\third_party\Razer-Wolverine-Battery-Widget.MIT.txt"; DestDir: "{app}\third_party"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\DrayBattery"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"
Name: "{autodesktop}\DrayBattery"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,DrayBattery}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{app}\{#AppExe}"; Parameters: "--quit"; Flags: runhidden; RunOnceId: StopDrayBattery
