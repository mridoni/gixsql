#define HOST_PLATFORM GetEnv('HOST_PLATFORM')
#define WORKSPACE GetEnv('WORKSPACE')
#define GIX_REVISION GetEnv('GIX_REVISION')
#define VER_GIXSQLMAJ GetEnv('GIXSQLMAJ')
#define VER_GIXSQLMIN GetEnv('GIXSQLMIN')
#define VER_GIXSQLREL GetEnv('GIXSQLREL')
#define DIST_DIR GetEnv('DIST_DIR')
#define MSVC_BUILD_TOOLS GetEnv('MSVC_BUILD_TOOLS')
#define MSVC_RUNTIME_X86 GetEnv('MSVC_RUNTIME_X86')
#define MSVC_RUNTIME_X64 GetEnv('MSVC_RUNTIME_X64')

#define INCLUDE_MSVC_LIBS GetEnv('INCLUDE_MSVC_LIBS')

#define CONFIG "Release"

#define P7ZIP "https://www.7-zip.org/a/7zr.exe"

[Setup]
AppName=GixSQL
AppVersion={#VER_GIXSQLMAJ}.{#VER_GIXSQLMIN}.{#VER_GIXSQLREL}-{#GIX_REVISION}
AppCopyright=Marco Ridoni
DefaultDirName={pf}\GixSQL
OutputDir={#WORKSPACE}\deploy\installers\gixsql-{#HOST_PLATFORM}
OutputBaseFilename=gixsql-{#VER_GIXSQLMAJ}.{#VER_GIXSQLMIN}.{#VER_GIXSQLREL}-{#GIX_REVISION}-installer
ArchitecturesInstallIn64BitMode=x64
DefaultGroupName=GixSQL
LicenseFile={#WORKSPACE}\LICENSE
RestartIfNeededByRun=False
DisableWelcomePage=False

[Files]
; main binaries
Source: "{#DIST_DIR}\bin\gixpp.exe"; DestDir: "{app}\bin"; Flags: ignoreversion createallsubdirs recursesubdirs
Source: "{#DIST_DIR}\lib\*"; DestDir: "{app}\lib"; Flags: ignoreversion createallsubdirs recursesubdirs
#if "x64" == HOST_PLATFORM
Source: "{#WORKSPACE}\redist\msvcrt\x64\*"; DestDir: "{app}\bin"; Flags: ignoreversion createallsubdirs recursesubdirs skipifsourcedoesntexist; Check: UseLocalMSVCRT
Source: "{#WORKSPACE}\redist\msvcrt\x64\*"; DestDir: "{app}\lib\x64\msvc"; Flags: ignoreversion createallsubdirs recursesubdirs skipifsourcedoesntexist; Check: UseLocalMSVCRT
#else
Source: "{#WORKSPACE}\redist\msvcrt\x86\*"; DestDir: "{app}\bin"; Flags: ignoreversion createallsubdirs recursesubdirs skipifsourcedoesntexist; Check: UseLocalMSVCRT
#endif
Source: "{#WORKSPACE}\redist\msvcrt\x86\*"; DestDir: "{app}\lib\x86\msvc"; Flags: ignoreversion createallsubdirs recursesubdirs skipifsourcedoesntexist; Check: UseLocalMSVCRT

; COPY files
Source: "{#WORKSPACE}\copy\SQLCA.cpy"; DestDir: "{app}\lib\copy"; Flags: ignoreversion createallsubdirs recursesubdirs

; examples and docs
Source: "{#WORKSPACE}\gixsql-tests-nunit\data\*.cbl"; DestDir: "{userdocs}\GixSQL\Examples"; Flags: ignoreversion createallsubdirs recursesubdirs onlyifdoesntexist
Source: "{#WORKSPACE}\gixsql-tests-nunit\data\*.cpy"; DestDir: "{userdocs}\GixSQL\Examples"; Flags: ignoreversion createallsubdirs recursesubdirs onlyifdoesntexist
Source: "{#WORKSPACE}\gixsql-tests-nunit\data\*.sql"; DestDir: "{userdocs}\GixSQL\Examples"; Flags: ignoreversion createallsubdirs recursesubdirs onlyifdoesntexist
Source: "{#WORKSPACE}\README.md"; DestDir: "{userdocs}\GixSQL\Documentation"; DestName: "README"; Flags: ignoreversion
Source: "{#WORKSPACE}\TESTING.md"; DestDir: "{userdocs}\GixSQL\Documentation"; DestName: "TESTING"; Flags: ignoreversion

; dependencies for DB runtime libraries
#if "x64" == HOST_PLATFORM
Source: "{#WORKSPACE}\gixsql\deploy\redist\pgsql\x64\msvc\*"; DestDir: "{app}\lib\x64\msvc"; Flags: ignoreversion createallsubdirs recursesubdirs skipifsourcedoesntexist
Source: "{#WORKSPACE}\gixsql\deploy\redist\pgsql\x64\gcc\*"; DestDir: "{app}\lib\x64\gcc"; Flags: ignoreversion createallsubdirs recursesubdirs skipifsourcedoesntexist

Source: "{#WORKSPACE}\gixsql\deploy\redist\mysql\x64\msvc\*"; DestDir: "{app}\lib\x64\msvc"; Flags: ignoreversion createallsubdirs recursesubdirs skipifsourcedoesntexist
Source: "{#WORKSPACE}\gixsql\deploy\redist\mysql\x64\gcc\*"; DestDir: "{app}\lib\x64\gcc"; Flags: ignoreversion createallsubdirs recursesubdirs skipifsourcedoesntexist
#endif

Source: "{#WORKSPACE}\gixsql\deploy\redist\pgsql\x86\msvc\*"; DestDir: "{app}\lib\x86\msvc"; Flags: ignoreversion createallsubdirs recursesubdirs skipifsourcedoesntexist
Source: "{#WORKSPACE}\gixsql\deploy\redist\pgsql\x86\gcc\*"; DestDir: "{app}\lib\x86\gcc"; Flags: ignoreversion createallsubdirs recursesubdirs skipifsourcedoesntexist

Source: "{#WORKSPACE}\gixsql\deploy\redist\mysql\x86\msvc\*"; DestDir: "{app}\lib\x86\msvc"; Flags: ignoreversion createallsubdirs recursesubdirs skipifsourcedoesntexist
Source: "{#WORKSPACE}\gixsql\deploy\redist\mysql\x86\gcc\*"; DestDir: "{app}\lib\x86\gcc"; Flags: ignoreversion createallsubdirs recursesubdirs skipifsourcedoesntexist

[Run]
#if "x64" == HOST_PLATFORM
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /passive /norestart"; WorkingDir: "{tmp}"; Flags: waituntilterminated runascurrentuser shellexec skipifdoesntexist; Description: "Visual C++ 2022 redistributable package (x64)"; Verb: "runas"; Check: UseDownloadedMSVCRT
#endif
Filename: "{tmp}\vc_redist.x86.exe"; Parameters: "/install /passive /norestart"; WorkingDir: "{tmp}"; Flags: waituntilterminated runascurrentuser shellexec skipifdoesntexist; Description: "Visual C++ 2022 redistributable package (x86)"; Verb: "runas"; Check: UseDownloadedMSVCRT

[Registry]
Root: "HKLM"; Subkey: "Software\MediumGray\gixsql"; ValueType: string; ValueName: "version"; ValueData: "{#VER_GIXSQLMAJ}.{#VER_GIXSQLMIN}.{#VER_GIXSQLREL}-{#GIX_REVISION}"; Flags: createvalueifdoesntexist deletevalue uninsdeletekey
Root: "HKLM"; Subkey: "Software\MediumGray\gixsql"; ValueType: string; ValueName: "HomeDir"; ValueData: "{app}"; Flags: createvalueifdoesntexist deletevalue uninsdeletekey
Root: "HKA"; Subkey: "Software\MediumGray\gixsql"; ValueType: string; ValueName: "DataDir"; ValueData: "{localappdata}\Gix"; Flags: createvalueifdoesntexist deletevalue uninsdeletekey
Root: "HKLM"; Subkey: "Software\MediumGray\gixsql"; Flags: uninsdeletekey
Root: "HKA"; Subkey: "Software\MediumGray\gixsql"; Flags: uninsdeletekey

[Dirs]
Name: "{app}\bin"
Name: "{app}\lib\copy"
Name: "{app}\lib\{#HOST_PLATFORM}\msvc"
Name: "{app}\lib\{#HOST_PLATFORM}\gcc"
Name: "{userdocs}\GixSQL"
Name: "{userdocs}\GixSQL\Documentation"
Name: "{userdocs}\GixSQL\Examples"

[Code]

#include "7zip.iss.inc"
const
  MSVCRT_LOCAL = 0;
  MSVCRT_DOWNLOAD = 1;
  MSVCRT_NOINSTALL = 2;
  
var
  DownloadPage: TDownloadWizardPage;
  MsvcRtOptPage: TInputOptionWizardPage;
  MsvcRtOptCheckListBox: TNewCheckListBox;
  optMsvcRuntime : Integer;
  CheckListBox: TNewCheckListBox;
  cbGCC, cbMSVC : Integer;

function OnDownloadProgress(const Url, FileName: String; const Progress, ProgressMax: Int64): Boolean;
begin
  if Progress = ProgressMax then
    Log(Format('Successfully downloaded file to {tmp}: %s', [FileName]));
  Result := True;
end;

procedure InitializeWizard;
begin
  DownloadPage := CreateDownloadPage(SetupMessage(msgWizardPreparing), SetupMessage(msgPreparingDesc), @OnDownloadProgress);
  
  optMsvcRuntime := MSVCRT_NOINSTALL;
  
  MsvcRtOptPage := CreateInputOptionPage(wpLicense, 
    'VC++ runtime', 'Choose how you want to install the Microsoft VC++ runtime', 
    'The Microsoft VC++ runtime is needed for Gix-IDE and for the runtime libraries of GixSQL. You can choose how you want to perform the install.',
    True, True);

  MsvcRtOptPage.Add('Use the embedded version and install for Gix-IDE/GixSQL only');
  MsvcRtOptPage.Add('Download and install for all applications (requires administrative rights)');
  MsvcRtOptPage.Add('Do not install');
  MsvcRtOptPage.Values[0] := True;  
end;

procedure CurPageChanged(CurPageID: Integer);
var
  i: Integer;
	cbtmp : Integer;
	crow : String;
  is_checked : Boolean;
	release_tag, id, version, host, target, linker, description : String;
begin

end;

function ShouldSkipPage(PageID: Integer): Boolean;
var
  release_tag, id, version, host, target, linker, description : String;
begin
  if (PageID = MsvcRtOptPage.ID) then
  begin
    if '{#INCLUDE_MSVC_LIBS}' = '0' then
    begin
      Log('No MSVC libraries will be included, so non MSVC runtime needed');
      Result:= True;
      Exit;
    end;
    
    Result := False;
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  i, chk_count, cidx : Integer;
  crow, curl: String;
  release_tag, id, version, host, target, linker, description : String;
begin
   
  if CurPageID = wpReady then begin
    DownloadPage.Clear;
    
    DownloadPage.Add('{#P7ZIP}', '7zr.exe', '');
    DownloadPage.Add('{#MSVC_RUNTIME_X86}', 'vc_redist.x86.exe', '');
    if '{#HOST_PLATFORM}' = 'x64' then
    begin
      DownloadPage.Add('{#MSVC_RUNTIME_X64}', 'vc_redist.x64.exe', '');
    end;
    
    DownloadPage.Show;
    try
      try
        DownloadPage.Download; 
        // TODO: check signatures        
        
        Result := True;
      except
        if DownloadPage.AbortedByUser then
          Log('Aborted by user.')
        else
          SuppressibleMsgBox(AddPeriod(GetExceptionMessage), mbCriticalError, MB_OK, IDOK);
        Result := False;
      end;
    finally
      DownloadPage.Hide;
    end;
  end 
  else
  begin
    Result := True;
  end;
  
  if CurPageID = MsvcRtOptPage.ID then
  begin
    optMsvcRuntime := MsvcRtOptPage.SelectedValueIndex;
    Log('MSVCRT install option: ' + IntToStr(optMsvcRuntime));
  end;    
	
end;

function UseDownloadedMSVCRT : Boolean;
begin
  Result := optMsvcRuntime = MSVCRT_DOWNLOAD;
end;

function UseLocalMSVCRT : Boolean;
begin
  Result := (optMsvcRuntime = MSVCRT_LOCAL) and ('{#INCLUDE_MSVC_LIBS}' = '1');
end;