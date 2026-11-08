; Installer di RegiScope (Inno Setup 6.3 o successivo).
; Lo usa GitHub (.github/workflows/installer.yml) per creare il setup a ogni nuova versione.
; In locale: crea-installer.bat compila l'app in Release e poi chiama ISCC su questo file.
;
; A mano:  ISCC.exe /DBuildDir=..\build-installer\Release RegiScope.iss

#define AppName      "RegiScope"
#ifndef AppVersion
  #define AppVersion "0.3"
#endif
#define AppPublisher "gaeros"
#define AppExe       "RegiScope.exe"

; Cartella dove CMake ha messo RegiScope.exe e le cartelle copiate accanto.
#ifndef BuildDir
  #define BuildDir "..\build-installer\Release"
#endif

[Setup]
; AppId identifica l'app per aggiornamenti e disinstallazione: non cambiarlo mai.
AppId={{6E2B7C1A-4F3D-4A8E-9B52-0D7E3C9A51F4}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL=https://github.com/gaeros/regiscope
AppSupportURL=https://github.com/gaeros/regiscope
; Installazione per l'utente corrente (niente richiesta di amministratore);
; nella prima schermata si può scegliere "per tutti gli utenti".
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
; Solo Windows a 64 bit (l'app è x64).
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
SetupIconFile=..\res\regiscope.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
OutputDir=output
OutputBaseFilename=RegiScope-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
; Se RegiScope è aperto durante un aggiornamento, chiede di chiuderlo.
CloseApplications=yes

[Languages]
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"

[Tasks]
; Casella spuntata di default: icona sul desktop.
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#BuildDir}\{#AppExe}";             DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\snippet_include\*";     DestDir: "{app}\snippet_include"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\snippet_runtime\*";     DestDir: "{app}\snippet_runtime"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#BuildDir}\esempi\*";              DestDir: "{app}\esempi"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\res\regiscope.ico";              DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md";                      DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE.md";                     DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\licenze\*";             DestDir: "{app}\licenze"; Flags: ignoreversion

[Icons]
; Menu Start
Name: "{group}\{#AppName}";                  Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; IconFilename: "{app}\regiscope.ico"
Name: "{group}\Cartella esempi";             Filename: "{app}\esempi"
Name: "{group}\Disinstalla {#AppName}";      Filename: "{uninstallexe}"
; Desktop (se la casella è spuntata)
Name: "{autodesktop}\{#AppName}";            Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; IconFilename: "{app}\regiscope.ico"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
