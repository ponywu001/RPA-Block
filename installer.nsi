; RPA-Block installer.
;
; Ships the deployed runtime directly rather than the self-extracting exe: that
; one unpacks into %LOCALAPPDATA% on first launch, and going through it here
; would mean installing a copy of the payload only to have it copied out again
; on the first run. Installed files start straight away.
;
; Built by package-installer.ps1, which stages the files and passes the paths
; in. Do not run makensis on this directly -- STAGE_DIR is required.

Unicode true

!ifndef STAGE_DIR
  !error "STAGE_DIR must be defined (use package-installer.ps1)"
!endif
!ifndef OUT_FILE
  !define OUT_FILE "dist\RPA-Block-Setup.exe"
!endif
!ifndef VERSION
  !define VERSION "0.1.0"
!endif

!define PRODUCT "RPA-Block"
!define PUBLISHER "SuChenAI"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT}"

!include "MUI2.nsh"
!include "FileFunc.nsh"

Name "${PRODUCT} ${VERSION}"
OutFile "${OUT_FILE}"
InstallDir "$PROGRAMFILES64\${PRODUCT}"
InstallDirRegKey HKLM "Software\${PUBLISHER}\${PRODUCT}" "InstallDir"
; Program Files needs it, and so does writing the uninstall entry to HKLM.
RequestExecutionLevel admin
SetCompressor /SOLID lzma

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${PRODUCT}"
VIAddVersionKey "CompanyName" "${PUBLISHER}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "${PRODUCT} 安裝程式"
VIAddVersionKey "LegalCopyright" "${PUBLISHER}"

!define MUI_ABORTWARNING
!define MUI_ICON "rpa-studio\resources\app.ico"
!define MUI_UNICON "rpa-studio\resources\app.ico"

!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\rpa-studio.exe"
!define MUI_FINISHPAGE_RUN_TEXT "立即啟動 ${PRODUCT}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "TradChinese"

; NSIS itself is a 32-bit program, so without these two the install looks fine
; and is subtly wrong on every machine: registry writes land under
; WOW6432Node where a 64-bit application would not look for them, and shortcuts
; go to the installing administrator's own Start Menu rather than to every
; user's -- so whoever actually uses the machine never sees one.
Function .onInit
  SetRegView 64
  SetShellVarContext all

  ; Upgrade path. Overwriting in place looks like it works, and leaves behind
  ; every file a later version stopped shipping -- a stale DLL next to the exe
  ; is found before anything else, so the orphans are not merely clutter.
  ; Removing the old install first is the only way to be sure what is on disk
  ; is what this installer put there.
  ;
  ; Flows, recordings and settings are untouched by this: they live in the
  ; user's Documents folder and in HKCU, not in the install directory.
  ReadRegStr $R0 HKLM "${UNINST_KEY}" "UninstallString"
  StrCmp $R0 "" done

  ReadRegStr $R1 HKLM "${UNINST_KEY}" "DisplayVersion"
  ReadRegStr $R2 HKLM "${UNINST_KEY}" "InstallLocation"

  IfSilent uninstall
  MessageBox MB_YESNO|MB_ICONQUESTION \
    "已經安裝了 RPA-Block $R1。$\n$\n要先移除舊版，再安裝 ${VERSION} 嗎？$\n流程檔與設定不會被刪除。" \
    IDYES uninstall
  Abort

  uninstall:
    ; Built from InstallLocation rather than from UninstallString: that value is
    ; already quoted, and quoting it again produces ""C:\...\uninstall.exe"",
    ; which fails to start while ExecWait reports nothing wrong -- the install
    ; then continues on top of the old files, which is the exact outcome this
    ; block exists to prevent.
    ;
    ; `_?=` keeps the uninstaller running from its own directory instead of
    ; copying itself to temp; without it ExecWait returns immediately and the
    ; new files get written while the old ones are still being deleted.
    ExecWait '"$R2\uninstall.exe" /S _?=$R2' $R3
    Delete "$R2\uninstall.exe"
    RMDir "$R2"

    StrCmp $R3 "0" +2 0
      MessageBox MB_ICONEXCLAMATION \
        "移除舊版時發生問題（代碼 $R3），安裝會繼續，但舊檔案可能殘留。"

  done:
FunctionEnd

Function un.onInit
  SetRegView 64
  SetShellVarContext all
FunctionEnd

Section "應用程式" SecMain
  SectionIn RO

  ; Refuse to install over a running copy: the files would be locked, half the
  ; install would succeed, and the result is a broken directory that looks
  ; installed.
  ;
  ; `find` exits 0 when it matches and 1 when it does not, which avoids pulling
  ; in string helpers just to search one line of tasklist output.
  nsExec::Exec 'cmd /c tasklist /FI "IMAGENAME eq rpa-studio.exe" /NH | find /I "rpa-studio.exe"'
  Pop $0
  StrCmp $0 "0" 0 +3
    MessageBox MB_ICONSTOP "RPA-Block 正在執行，請先關閉它再安裝。"
    Abort

  SetOutPath "$INSTDIR"
  File /r "${STAGE_DIR}\*.*"

  WriteRegStr HKLM "Software\${PUBLISHER}\${PRODUCT}" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\${PUBLISHER}\${PRODUCT}" "Version" "${VERSION}"

  ; Add/Remove Programs.
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "${PRODUCT}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\rpa-studio.exe"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
  WriteRegStr HKLM "${UNINST_KEY}" "QuietUninstallString" "$\"$INSTDIR\uninstall.exe$\" /S"
  WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1

  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  IntFmt $0 "0x%08X" $0
  WriteRegDWORD HKLM "${UNINST_KEY}" "EstimatedSize" "$0"

  WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

Section "開始功能表捷徑" SecStartMenu
  CreateDirectory "$SMPROGRAMS\${PRODUCT}"
  CreateShortcut "$SMPROGRAMS\${PRODUCT}\${PRODUCT}.lnk" "$INSTDIR\rpa-studio.exe"
  CreateShortcut "$SMPROGRAMS\${PRODUCT}\解除安裝 ${PRODUCT}.lnk" "$INSTDIR\uninstall.exe"
SectionEnd

Section "桌面捷徑" SecDesktop
  CreateShortcut "$DESKTOP\${PRODUCT}.lnk" "$INSTDIR\rpa-studio.exe"
SectionEnd

LangString DESC_SecMain ${LANG_TRADCHINESE} "主程式、Qt 執行期、OCR 模型與範例流程。"
LangString DESC_SecStartMenu ${LANG_TRADCHINESE} "在開始功能表建立捷徑。"
LangString DESC_SecDesktop ${LANG_TRADCHINESE} "在桌面建立捷徑。"

Section "Uninstall"
  ; Deliberately left alone: flows, recordings, published scripts and run
  ; history live in the user's Documents folder, and an uninstaller that
  ; deletes a customer's automations is not a tidy uninstaller. Settings in
  ; HKCU stay too, so a reinstall finds its configuration.
  Delete "$INSTDIR\uninstall.exe"
  RMDir /r "$INSTDIR\models"
  RMDir /r "$INSTDIR\examples"
  RMDir /r "$INSTDIR\platforms"
  RMDir /r "$INSTDIR\imageformats"
  RMDir /r "$INSTDIR\styles"
  RMDir /r "$INSTDIR\generic"
  RMDir /r "$INSTDIR\networkinformation"
  RMDir /r "$INSTDIR\tls"
  Delete "$INSTDIR\*.exe"
  Delete "$INSTDIR\*.dll"
  Delete "$INSTDIR\*.txt"
  RMDir "$INSTDIR"

  Delete "$SMPROGRAMS\${PRODUCT}\${PRODUCT}.lnk"
  Delete "$SMPROGRAMS\${PRODUCT}\解除安裝 ${PRODUCT}.lnk"
  RMDir "$SMPROGRAMS\${PRODUCT}"
  Delete "$DESKTOP\${PRODUCT}.lnk"

  DeleteRegKey HKLM "${UNINST_KEY}"
  DeleteRegKey HKLM "Software\${PUBLISHER}\${PRODUCT}"
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} $(DESC_SecMain)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} $(DESC_SecStartMenu)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} $(DESC_SecDesktop)
!insertmacro MUI_FUNCTION_DESCRIPTION_END
