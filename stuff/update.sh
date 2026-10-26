#!/bin/bash
eb=$(cd /mnt/work/sdisk/os/aq/etc/local/overlay/sys-apps/mxev && echo *.ebuild)
ver=$(echo $eb | sed -E 's/.*\.([0-9]+)\.ebuild/\1/')
newver=$(expr 1 + "${ver}" 2>/dev/null || echo 0)

mxev-sed() {
  case "$1" in
    usb)
      sed '\|^ExecStart=/usr/bin/mxev /etc/mxev.key absent|d' "$2"
      ;;
    absent)
      sed '\|^ExecStart=/usr/bin/mxev /etc/mxev.key /dev|d' "$2"
      ;;
    *)
      echo "usage: mxev-keep {usb|absent} FILE" >&2
      return 1
      ;;
  esac
}

rm -f mxev.tar mxev.tar.zst mxev.service mxev.service.skel Manifest *.ebuild
cat > mxev-0.0.0.${newver}.ebuild << "__EOF__"
EAPI=8

inherit cmake fcaps systemd

DESCRIPTION="mxev"
SRC_URI=""

LICENSE="GPL-2"
SLOT="0"
KEYWORDS="~amd64 ~x86"
IUSE="+filecaps"

DEPEND=""
RDEPEND="
    acct-group/mxev
    acct-user/mxev
"
BDEPEND=""

FILECAPS=(
    cap_net_raw usr/bin/mxev
)

src_unpack() {
    mkdir -p "${WORKDIR}/mxev-${PV}"
    tar -C "${WORKDIR}/mxev-${PV}" -xf "${FILESDIR}/mxev.tar.zst"
}

src_configure() {
    local mycmakeargs=(
        -DMX_ROUTER=ON
    )
    cmake_src_configure
}

src_install() {
    cmake_src_install
    systemd_dounit "${FILESDIR}/mxev.service"
}

pkg_postinst() {
    fcaps_pkg_postinst

    if [[ -f "${EROOT}/etc/mxev.conf" ]]; then
        chown root:mxev "${EROOT}/etc/mxev.conf" || true
        chmod 0640 "${EROOT}/etc/mxev.conf" || true
    fi
}
__EOF__
cat > mxev.service.skel << "__EOF__"
[Unit]
Before=display-manager.service
After=systemd-networkd-wait-online@eth0.service
PartOf=systemd-networkd-wait-online@eth0.service

[Service]
Type=simple
Nice=-8
IOSchedulingClass=realtime
IOSchedulingPriority=1
User=mxev
Group=mxev
ExecStart=/usr/bin/mxev /etc/mxev.key /dev/input/by-id/usb-Gaming_KB_Gaming_KB-event-kbd /dev/input/by-id/usb-17ef_Lenovo_Optical_Mouse-event-mouse
ExecStart=/usr/bin/mxev /etc/mxev.key absent absent

[Install]
WantedBy=multi-user.target
__EOF__
tar -cf mxev.tar ../CMakeLists.txt ../src 
zstd --ultra -22 mxev.tar 
sub2=$(mxev-sed usb mxev.service.skel | b2sum | sed "s/ .*//")
sus5=$(mxev-sed usb mxev.service.skel | sha512sum | sed "s/ .*//")
susz=$(mxev-sed usb mxev.service.skel | wc -c )
sab2=$(mxev-sed absent mxev.service.skel | b2sum | sed "s/ .*//")
sas5=$(mxev-sed absent mxev.service.skel | sha512sum | sed "s/ .*//")
sasz=$(mxev-sed absent mxev.service.skel | wc -c )
fb2=$(b2sum mxev.tar.zst | sed "s/ .*//")
fs5=$(sha512sum mxev.tar.zst | sed "s/ .*//")
fsz=$(stat --format="%s" mxev.tar.zst)
eb2=$(b2sum mxev-0.0.0.${newver}.ebuild | sed "s/ .*//")
es5=$(sha512sum mxev-0.0.0.${newver}.ebuild | sed "s/ .*//")
esz=$(stat --format="%s" mxev-0.0.0.${newver}.ebuild)
new_key_file="$(mktemp)"
dd if=/dev/urandom of="${new_key_file}" bs=32 count=1 > /dev/null 2>&1
mx_key_file="/etc/mxev.key"
for i in xx aq ar as ax xx ; do 
    [ ! -d /mnt/work/sdisk/os/"${i}" ] && continue
    rm -f Manifest mxev.service
    if [ "${i}" != "as" ] ; then 
        echo "AUX mxev.service ${sasz} BLAKE2B ${sab2} SHA512 ${sas5}" >> Manifest
        mxev-sed absent mxev.service.skel > mxev.service
    else
        echo "AUX mxev.service ${susz} BLAKE2B ${sub2} SHA512 ${sus5}" >> Manifest
        mxev-sed usb mxev.service.skel > mxev.service
    fi
    echo "AUX mxev.tar.zst ${fsz} BLAKE2B ${fb2} SHA512 ${fs5}" >> Manifest
    echo "EBUILD mxev-0.0.0.${newver}.ebuild ${esz} BLAKE2B ${eb2} SHA512 ${es5}" >> Manifest
    [ ! -f /mnt/work/sdisk/os/"${i}""${mx_key_file}" ] && scp -q "${new_key_file}" root@[::1]:/mnt/work/sdisk/os/"${i}""${mx_key_file}"
    ssh root@::1 chown mxev:mxev /mnt/work/sdisk/os/"${i}""${mx_key_file}"
    ssh root@::1 chmod 400 /mnt/work/sdisk/os/"${i}""${mx_key_file}"
    ssh root@::1 rm /mnt/work/sdisk/os/${i}/etc/local/overlay/sys-apps/mxev/*.ebuild
    ssh root@::1 cp $PWD/mxev.service /mnt/work/sdisk/os/"${i}"/etc/local/overlay/sys-apps/mxev/files 
    ssh root@::1 cp $PWD/mxev.tar.zst /mnt/work/sdisk/os/"${i}"/etc/local/overlay/sys-apps/mxev/files 
    ssh root@::1 cp ${PWD}/{Manifest,mxev-0.0.0.${newver}.ebuild} /mnt/work/sdisk/os/"${i}"/etc/local/overlay/sys-apps/mxev/
done
rm -f mxev.tar mxev.tar.zst Manifest mxev.service mxev.service.skel *.ebuild "${new_key_file}"
