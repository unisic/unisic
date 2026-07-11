# CPack RPM %post: configure the Unisic COPR repository on first install
# (Fedora only — openSUSE users install from the OBS repo, which is added
# manually per README) so later versions arrive through `dnf upgrade`.
# copr-postun.sh removes it on erase. Spec-built rpms (COPR/OBS) never run
# this — they are installed from a repo already.
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
fi
