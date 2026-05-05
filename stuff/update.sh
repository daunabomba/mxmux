#!/bin/bash
eb=$(cd /mnt/work/sdisk/os/aq/etc/local/overlay/sys-apps/mxev && echo *.ebuild)
ver=$(echo $eb | sed -E 's/.*\.([0-9]+)\.ebuild/\1/')
newver=$(expr 1 + "${ver}" 2>/dev/null || echo 0)

rm -f mxev.tar mxev.tar.zst Manifest *.ebuild
cat > mxev-0.0.0.${newver}.ebuild << __EOF__
EAPI=8

inherit cmake fcaps

DESCRIPTION="mxev"
SRC_URI=""

LICENSE="GPL-2"
SLOT="0"
KEYWORDS="~amd64 ~x86"
IUSE="+filecaps"

DEPEND=""
RDEPEND="\${DEPEND}"
BDEPEND=""

FILECAPS=( 
    cap_net_raw usr/bin/mxev 
)

src_unpack() {
    mkdir -p "\${WORKDIR}/mxev-\${PV}"
    tar -C "\${WORKDIR}/mxev-\${PV}" -xf "\${FILESDIR}/mxev.tar.zst"
}

src_configure() {
    local mycmakeargs=(
        -DMX_ROUTER=ON
    )
    cmake_src_configure
}
__EOF__
tar -cf mxev.tar ../CMakeLists.txt ../src 
zstd --ultra -22 mxev.tar 
fb2=$(b2sum mxev.tar.zst | sed "s/ .*//")
fs5=$(sha512sum mxev.tar.zst | sed "s/ .*//")
fsz=$(stat --format="%s" mxev.tar.zst)
eb2=$(b2sum mxev-0.0.0.${newver}.ebuild | sed "s/ .*//")
es5=$(sha512sum mxev-0.0.0.${newver}.ebuild | sed "s/ .*//")
esz=$(stat --format="%s" mxev-0.0.0.${newver}.ebuild)
echo "AUX mxev.tar.zst ${fsz} BLAKE2B ${fb2} SHA512 ${fs5}" >> Manifest
echo "EBUILD mxev-0.0.0.${newver}.ebuild ${esz} BLAKE2B ${eb2} SHA512 ${es5}" >> Manifest
new_key_file="$(mktemp)"
dd if=/dev/urandom of="${new_key_file}" bs=32 count=1 2&>1 > /dev/null
mx_key_file="/etc/mxev.key"
for i in aq ar as ; do 
    scp "${new_key_file}" root@[::1]:/mnt/work/sdisk/os/"${i}""${mx_key_file}"
    ssh root@::1 chown mxev:mxev /mnt/work/sdisk/os/"${i}""${mx_key_file}"
    ssh root@::1 chmod 400 /mnt/work/sdisk/os/"${i}""${mx_key_file}"
    ssh root@::1 rm /mnt/work/sdisk/os/${i}/etc/local/overlay/sys-apps/mxev/*.ebuild
    ssh root@::1 cp $PWD/mxev.tar.zst /mnt/work/sdisk/os/"${i}"/etc/local/overlay/sys-apps/mxev/files 
    ssh root@::1 cp ${PWD}/{Manifest,mxev-0.0.0.${newver}.ebuild} /mnt/work/sdisk/os/"${i}"/etc/local/overlay/sys-apps/mxev/
done
rm -f mxev.tar mxev.tar.zst Manifest *.ebuild "${new_key_file}"
