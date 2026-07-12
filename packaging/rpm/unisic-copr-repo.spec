# Repo-installer package: the ONLY rpm attached to GitHub releases. A binary
# rpm cannot span distros (QML registration links Qt_6.x_PRIVATE_API symbols,
# pinning it to the build root's exact Qt minor), so the release asset instead
# configures the COPR repository — whose chroots build unisic against each
# distro's own Qt — and the app is installed from there:
#   sudo dnf install ./unisic-copr-repo-*.noarch.rpm && sudo dnf install unisic
#   sudo zypper --no-gpg-checks install ./unisic-copr-repo-*.noarch.rpm && sudo zypper install unisic
# The %post zypp branch must stay in sync with packaging/rpm/copr-post.sh
# (the same logic run by locally cpack-built rpms).

%global copr_base https://download.copr.fedorainfracloud.org/results/deandark/Unisic

Name:           unisic-copr-repo
Version:        %{?repo_version}%{!?repo_version:1}
Release:        1
Summary:        COPR repository configuration for Unisic (Fedora and openSUSE)
License:        GPL-3.0-or-later
URL:            https://github.com/unisic/unisic
BuildArch:      noarch

%description
Configures the Unisic COPR repository (deandark/Unisic) so unisic installs
and updates through the native package manager: a dnf repo on Fedora, a zypp
repo on openSUSE Tumbleweed / Leap 15.6. Contains no binaries — install the
"unisic" package from the repository afterwards.

%install
# The dnf repo file ships statically — correct on Fedora, inert on openSUSE
# (dnf is not the manager there; the unowned /etc/yum.repos.d dir is created
# by rpm). The zypp file cannot be static: Tumbleweed and Leap need different
# baseurl paths, so %%post writes the right one and %%ghost tracks it for
# removal on erase.
install -d %{buildroot}%{_sysconfdir}/yum.repos.d
cat > %{buildroot}%{_sysconfdir}/yum.repos.d/unisic-copr.repo <<'EOF'
[copr:copr.fedorainfracloud.org:deandark:Unisic]
name=Copr repo for Unisic owned by deandark
baseurl=%{copr_base}/fedora-$releasever-$basearch/
type=rpm-md
skip_if_unavailable=True
gpgcheck=1
gpgkey=%{copr_base}/pubkey.gpg
repo_gpgcheck=0
enabled=1
enabled_metadata=1
EOF
install -d %{buildroot}%{_sysconfdir}/zypp/repos.d
touch %{buildroot}%{_sysconfdir}/zypp/repos.d/unisic-copr.repo

%post
if [ -r /etc/os-release ]; then
    . /etc/os-release
    case "$ID" in
    opensuse-tumbleweed|opensuse-slowroll)
        chroot_dir="opensuse-tumbleweed-\$basearch" ;;
    opensuse-leap)
        chroot_dir="opensuse-leap-\$releasever-\$basearch" ;;
    *)
        chroot_dir="" ;;
    esac
    if [ -n "$chroot_dir" ] && [ -d /etc/zypp/repos.d ]; then
        # COPR signs PACKAGES but not the repo metadata (repomd.xml) — mirror
        # dnf's repo_gpgcheck=0 or zypper rejects the repo on first refresh.
        cat > /etc/zypp/repos.d/unisic-copr.repo <<EOF
[unisic-copr]
name=Copr repo for Unisic owned by deandark
baseurl=%{copr_base}/${chroot_dir}/
type=rpm-md
enabled=1
autorefresh=1
repo_gpgcheck=0
pkg_gpgcheck=1
gpgkey=%{copr_base}/pubkey.gpg
EOF
    fi
fi

%files
%config(noreplace) %{_sysconfdir}/yum.repos.d/unisic-copr.repo
%ghost %{_sysconfdir}/zypp/repos.d/unisic-copr.repo

%changelog
* Sun Jul 12 2026 Unisic maintainers <unisic@debondor.com> - 1-1
- Repo-installer package: configures the Unisic COPR repo on Fedora and
  openSUSE; replaces the binary rpm on the GitHub release page.
