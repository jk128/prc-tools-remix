Name: prc-tools
# The version line is grepped for by configure.  It must be exactly
# Version<colon><space><versionnumber><newline>
Version: 2.0.93
Release: 1
Summary: GCC and related tools for Palm OS development
License: GPL
URL: http://prc-tools.sourceforge.net/
Group: Development/Palm OS
Source0: http://prdownloads.sourceforge.net/prc-tools/%{name}-%{version}.tar.gz
Source1: ftp://sources.redhat.com/pub/binutils/releases/binutils-2.12.tar.gz
Source2: ftp://sources.redhat.com/pub/gdb/releases/gdb-5.0.tar.gz
Source3: ftp://gcc.gnu.org/pub/gcc/releases/gcc-2.95.3/gcc-2.95.3.tar.gz
Source4: ftp://ftp.gnu.org/pub/gnu/make/make-3.79.1.tar.gz
NoSource: 1
NoSource: 2
NoSource: 3
NoSource: 4
BuildRoot: %{_tmppath}/%{name}-root
BuildRequires: texinfo

# The target used to be 'm68k-palmos-coff'.  Some people may want to leave
# it thus to avoid changing their makefiles a little bit.
%define target m68k-palmos

# This is the canonical place to look for Palm OS-related header files and
# such on Unix-like file systems.
%define palmdev_prefix /opt/palmdev

%description
A complete compiler tool chain for building Palm OS applications in C or C++.
Includes (patched versions of) binutils 2.12, gdb 5.0, and GCC 2.95.3, along
with various post-linker tools to produce Palm OS .prc files.

You will also need a Palm OS SDK and some way of creating resources, such as
PilRC.

%package htmldocs
Summary: GCC, binutils, gdb, make, and prc-tools documentation as HTML
Group: Development/Palm OS
Prefix: %{palmdev_prefix}
%description htmldocs
GCC, binutils, gdb, make, and general prc-tools documentation in HTML
format.  The various native development packages and the main prc-tools
package, respectively, provide exactly this documentation in info format.
This optional package is for those who prefer HTML-formatted documentation.

By default, this package will be installed at %{palmdev_prefix}/doc, and
you should point your web browser at %{palmdev_prefix}/doc/index.html.
If you want to install it elsewhere, you can do so via the prefix and/or
relocation facilities of your RPM installation tool.

%prep
%setup -n binutils-2.12 -T -b 1
%setup -n gdb-5.0 -T -b 2
%setup -n gcc-2.95.3 -T -b 3
%setup -n make-3.79.1 -T -b 4
%setup

cat *.palmos.diff | (cd .. && patch -p0)

mv ../binutils-2.12 binutils
mv ../gdb-5.0 gdb
mv ../gcc-2.95.3 gcc
mv ../make-3.79.1 make

# The patch touches a file this depends on, and you need autoconf to remake
# it.  There's no changes, so let's just touch it so people don't have to
# have autoconf installed.
touch gcc/gcc/cstamp-h.in

mkdir build
mkdir build/empty
mkdir build/static-libs

%build
cd build

# Ensure that we link *statically* against the stdc++ library
rm -f static-libs/*
ln -s `${CXX:-g++} -print-file-name=libstdc++.a` static-libs/libstdc++.a

# The --with-headers bit is a nasty hack to try to make fixinc happy on
# Solaris and simultaneously stop it from doing anything.
LDFLAGS=-L`pwd`/static-libs ../configure \
  --target=%{target} \
  --enable-languages=c,c++ \
  --with-headers=`pwd`/empty \
  --with-palmdev-prefix=%{palmdev_prefix} \
  --enable-html-docs=%{palmdev_prefix}/doc \
  --prefix=%{_prefix} --exec-prefix=%{_exec_prefix} \
  --bindir=%{_bindir} --sbindir=%{_sbindir} --libexecdir=%{_libexecdir} \
  --localstatedir=%{_localstatedir} --sharedstatedir=%{_sharedstatedir} \
  --sysconfdir=%{_sysconfdir} --datadir=%{_datadir} \
  --includedir=%{_includedir} --libdir=%{_libdir} \
  --mandir=%{_mandir} --infodir=%{_infodir}

make

%install
[ ${RPM_BUILD_ROOT:-/} != / ] && rm -rf $RPM_BUILD_ROOT
cd build
%makeinstall htmldir=$RPM_BUILD_ROOT%{palmdev_prefix}/doc

%clean
[ ${RPM_BUILD_ROOT:-/} != / ] && rm -rf $RPM_BUILD_ROOT

%post
# Given foo.info, install-info will check for both foo.info and foo.info.gz
if /bin/sh -c 'install-info --version' >/dev/null 2>&1; then
  install-info --info-dir=%{_infodir} %{_infodir}/prc-tools.info
fi

%preun
if [ "$1" = 0 ]; then
  if /bin/sh -c 'install-info --version' >/dev/null 2>&1; then
    install-info --remove --info-dir=%{_infodir} %{_infodir}/prc-tools.info
  fi
fi

%files
%defattr(-, root, root)
%{_bindir}/*
%{_exec_prefix}/%{target}
%{_libdir}/gcc-lib/%{target}
# Native packages provide gcc.info* etc, so we limit ourselves to this one
%doc %{_infodir}/prc-tools*
# Similarly, the native packages have already provided equivalent manpages
#%doc %{_mandir}/man1/*

%doc COPYING README

%files htmldocs
%doc %{palmdev_prefix}/doc
