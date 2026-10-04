; MeinLive Studio - Windows-Installer (Inno Setup 6)
;
; Wird von .github/workflows/meinlive-build.yaml gebaut:
;   iscc /DAppVersion=1.0.0 /DSourceDir=<cmake --install Ordner> /DVcRedist=<vc_redist.x64.exe> meinlive-studio.iss
;
; Update aus MeinLive Studio heraus (siehe frontend/meinlive/MeinLiveUpdate.cpp):
;   Setup.exe /SILENT /SP- /NOCANCEL /UPDATE   -> installiert still und startet MeinLive Studio neu

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\build_x64\install"
#endif

#define AppName "MeinLive Studio"
#define AppExe "bin\64bit\meinlive-studio.exe"

[Setup]
; Feste ID - niemals ändern, sonst erkennt Windows Updates nicht als dieselbe Anwendung
AppId={{5C0F2E7B-6B1D-4C55-9E0A-6D3B1C7E2A41}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=MeinLive
AppPublisherURL=https://meinlive.de
AppSupportURL=https://meinlive.de/support
AppUpdatesURL=https://meinlive.de/studio
AppCopyright=MeinLive Studio basiert auf OBS Studio (GPLv2)
VersionInfoVersion={#AppVersion}
VersionInfoProductName={#AppName}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=auto
OutputDir=Output
OutputBaseFilename=MeinLive-Studio-{#AppVersion}-Setup
SetupIconFile=..\..\frontend\cmake\windows\obs-studio.ico
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
WizardStyle=modern
WizardImageFile=wizard-large-164.bmp,wizard-large-410.bmp
WizardSmallImageFile=wizard-small-55.bmp,wizard-small-138.bmp
LicenseFile=..\..\COPYING
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.19041
PrivilegesRequired=admin
Compression=lzma2/max
SolidCompression=yes
; Eigener Prozess: mehr Speicher für die Kompression (ultra64 lief auf GitHub in "Out of memory")
LZMAUseSeparateProcess=yes
; Laufendes MeinLive Studio erkennt [Code] InitializeSetup selbst (Mutex "MeinLiveStudioCore",
; RunOnceMutex in frontend/utility/platform-windows.cpp). Bewusst KEIN AppMutex: das prüft sofort
; beim Start - bei einem Update aus dem Programm heraus beendet sich das alte Programm aber erst
; ein paar Sekunden später, dann brach Setup ab (1.0.0 -> 1.0.1, 04.10.2026).
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "en"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
de.VCRedist=Microsoft Visual C++ Laufzeitbibliothek wird installiert ...
en.VCRedist=Installing Microsoft Visual C++ runtime ...
de.VirtualCam=Virtuelle Kamera wird eingerichtet ...
en.VirtualCam=Setting up virtual camera ...
de.LaunchApp=MeinLive Studio jetzt starten
en.LaunchApp=Launch MeinLive Studio now
de.AppRunning=MeinLive Studio läuft noch. Bitte schließe das Programm und klicke dann auf „Wiederholen“.
en.AppRunning=MeinLive Studio is still running. Please close it and then click "Retry".

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
#ifdef VcRedist
Source: "{#VcRedist}"; DestDir: "{tmp}"; DestName: "vc_redist.x64.exe"; Flags: deleteafterinstall
#endif

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}\bin\64bit"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}\bin\64bit"; Tasks: desktopicon

[Run]
#ifdef VcRedist
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; StatusMsg: "{cm:VCRedist}"; Flags: waituntilterminated
#endif
Filename: "{sys}\regsvr32.exe"; Parameters: "/i /s ""{app}\data\obs-plugins\win-dshow\obs-virtualcam-module64.dll"""; StatusMsg: "{cm:VirtualCam}"; Flags: runhidden waituntilterminated
Filename: "{syswow64}\regsvr32.exe"; Parameters: "/i /s ""{app}\data\obs-plugins\win-dshow\obs-virtualcam-module32.dll"""; StatusMsg: "{cm:VirtualCam}"; Flags: runhidden waituntilterminated; Check: FileExists(ExpandConstant('{app}\data\obs-plugins\win-dshow\obs-virtualcam-module32.dll'))
; Normale Installation: Häkchen "jetzt starten"
Filename: "{app}\{#AppExe}"; WorkingDir: "{app}\bin\64bit"; Description: "{cm:LaunchApp}"; Flags: nowait postinstall skipifsilent runasoriginaluser
; Update aus dem Programm (/UPDATE): still installieren und danach wieder starten
Filename: "{app}\{#AppExe}"; WorkingDir: "{app}\bin\64bit"; Flags: nowait runasoriginaluser; Check: IsUpdate

[UninstallRun]
Filename: "{sys}\regsvr32.exe"; Parameters: "/u /s ""{app}\data\obs-plugins\win-dshow\obs-virtualcam-module64.dll"""; Flags: runhidden; RunOnceId: "VirtualCam64"
Filename: "{syswow64}\regsvr32.exe"; Parameters: "/u /s ""{app}\data\obs-plugins\win-dshow\obs-virtualcam-module32.dll"""; Flags: runhidden; RunOnceId: "VirtualCam32"

[Code]
const
  AppMutexName = 'MeinLiveStudioCore';

function IsUpdate: Boolean;
begin
  Result := WizardSilent and (Pos('/UPDATE', Uppercase(GetCmdTail)) > 0);
end;

function InitializeSetup: Boolean;
var
  i: Integer;
begin
  Result := True;
  if Pos('/UPDATE', Uppercase(GetCmdTail)) > 0 then
  begin
    { Update aus MeinLive Studio: bis zu 30 s warten, bis sich das alte Programm beendet hat }
    for i := 1 to 60 do
    begin
      if not CheckForMutexes(AppMutexName) then
        Exit;
      Sleep(500);
    end;
  end;
  { Läuft MeinLive Studio (noch), um Schließen bitten }
  while CheckForMutexes(AppMutexName) do
  begin
    if SuppressibleMsgBox(ExpandConstant('{cm:AppRunning}'), mbError, MB_RETRYCANCEL, IDCANCEL) = IDCANCEL then
    begin
      Result := False;
      Exit;
    end;
  end;
end;
