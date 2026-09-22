SUMMARY = "ECO fullscreen Wayland and audio session"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${ECO_ROOT}/LICENSE;md5=6f66b87b4697de4f2e4ea02b091e4ea3"
SRC_URI = "file://eco-session.service file://eco-session file://20-eco.conf"
S = "${WORKDIR}"
inherit systemd
SYSTEMD_SERVICE:${PN} = "eco-session.service"
RDEPENDS:${PN} = "bash dbus pipewire pipewire-pulse wireplumber eco-workstation weston-init"
do_install() {
    install -Dm0644 ${WORKDIR}/eco-session.service ${D}${systemd_system_unitdir}/eco-session.service
    install -Dm0755 ${WORKDIR}/eco-session ${D}${libexecdir}/eco-session
    install -Dm0644 ${WORKDIR}/20-eco.conf ${D}${sysconfdir}/pipewire/pipewire.conf.d/20-eco.conf
}
FILES:${PN} += "${systemd_system_unitdir} ${libexecdir} ${sysconfdir}/pipewire"
