# CPack RPM %post: configure the Unisic COPR repository on first install so
# later versions arrive through `dnf upgrade` / `zypper up`. Fedora writes a
# dnf repo, openSUSE a zypp one (COPR builds opensuse-tumbleweed and
# opensuse-leap-15.6 chroots via Packit). copr-postun.sh removes the entry on
# erase. Spec-built rpms (installed FROM the COPR repo) never run this — they
# are installed from a repo already.
if [ "$1" -ge 1 ] && [ -r /etc/os-release ]; then
    . /etc/os-release
    if [ "$ID" = "fedora" ] && [ ! -e /etc/yum.repos.d/unisic-copr.repo ]; then
        cat > /etc/yum.repos.d/unisic-copr.repo <<'EOF'
[copr:copr.fedorainfracloud.org:deandark:Unisic]
name=Copr repo for Unisic owned by deandark
baseurl=https://download.copr.fedorainfracloud.org/results/deandark/Unisic/fedora-$releasever-$basearch/
type=rpm-md
skip_if_unavailable=True
gpgcheck=1
gpgkey=https://download.copr.fedorainfracloud.org/results/deandark/Unisic/pubkey.gpg
repo_gpgcheck=0
enabled=1
enabled_metadata=1
EOF
    fi
    case "$ID" in
    opensuse-tumbleweed|opensuse-slowroll)
        chroot_dir="opensuse-tumbleweed-\$basearch" ;;
    opensuse-leap)
        chroot_dir="opensuse-leap-\$releasever-\$basearch" ;;
    *)
        chroot_dir="" ;;
    esac
    if [ -n "$chroot_dir" ] && [ -d /etc/zypp/repos.d ] \
        && [ ! -e /etc/zypp/repos.d/unisic-copr.repo ]; then
        # COPR signs PACKAGES but not the repo metadata (repomd.xml) — mirror
        # dnf's repo_gpgcheck=0 or zypper rejects the repo on first refresh.
        cat > /etc/zypp/repos.d/unisic-copr.repo <<EOF
[unisic-copr]
name=Copr repo for Unisic owned by deandark
baseurl=https://download.copr.fedorainfracloud.org/results/deandark/Unisic/${chroot_dir}/
type=rpm-md
enabled=1
autorefresh=1
repo_gpgcheck=0
pkg_gpgcheck=1
gpgkey=https://download.copr.fedorainfracloud.org/results/deandark/Unisic/pubkey.gpg
EOF
    fi
fi
