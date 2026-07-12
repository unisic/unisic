# CPack RPM %postun: $1 == 0 means full erase (an upgrade passes 1) — take
# the repo entry this package's %post created along with it.
if [ "$1" -eq 0 ]; then
    rm -f /etc/yum.repos.d/unisic-copr.repo
fi
