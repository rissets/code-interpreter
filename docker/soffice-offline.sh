#!/bin/sh
# Each conversion gets a disposable profile outside persisted user artifacts.
office_has_profile=false
for office_arg do
    case "$office_arg" in -env:UserInstallation=*) office_has_profile=true;; esac
done
if [ "$office_has_profile" = false ]; then
    office_profile=$(mktemp -d /tmp/bpbatam-office.XXXXXX) || exit 1
    trap 'rm -rf "$office_profile"' EXIT
    set -- "-env:UserInstallation=file://$office_profile" "$@"
fi
export SAL_USE_VCLPLUGIN=svp
export FONTCONFIG_FILE=/usr/share/office-fontconfig/fonts.conf
export FONTCONFIG_PATH=/usr/share/office-fontconfig
export LD_PRELOAD=/usr/local/lib/office-anonymous-ipc.so
/usr/lib/libreoffice/program/soffice.bin "$@"
office_status=$?
# LibreOffice asks its launcher to restart after first-profile initialization.
if [ "$office_status" = 81 ]; then
    /usr/lib/libreoffice/program/soffice.bin "$@"
    office_status=$?
fi
exit "$office_status"
