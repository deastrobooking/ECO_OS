SUMMARY = "ECO Music OS open developer workstation image"
LICENSE = "MIT"
inherit core-image
# Console root access is deliberate in the developer image. No SSH server.
IMAGE_FEATURES += "debug-tweaks"
IMAGE_INSTALL:append = " \
    eco-workstation eco-session \
    weston weston-init weston-examples \
    pipewire pipewire-alsa pipewire-jack pipewire-pulse pipewire-tools pipewire-modules-meta pipewire-spa-plugins-meta \
    wireplumber alsa-utils alsa-utils-aconnect \
    qtwayland qtwayland-plugins qtdeclarative-qmlplugins \
    kernel-modules dejavu-sans-fonts \
"
IMAGE_ROOTFS_EXTRA_SPACE = "1048576"
