#
# Do NOT Edit the Auto-generated Part!
# Generated by: spectacle version 0.27
#

Name:       connman-qt

Summary:    Qt bindings for connman
Version:    1.0.86
Release:    1
Group:      System/GUI/Other
License:    Apache License
URL:        https://github.com/nemomobile/libconnman-qt.git
Source0:    %{name}-%{version}.tar.bz2
Requires:   connman >= 1.24+git8
Requires(post): /sbin/ldconfig
Requires(postun): /sbin/ldconfig
BuildRequires:  pkgconfig(QtCore) >= 4.6.0
BuildRequires:  pkgconfig(QtNetwork)
BuildRequires:  pkgconfig(QtDBus)
BuildRequires:  pkgconfig(QtGui)
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  doxygen
BuildRequires:  qt-devel-tools

%description
This is a library for working with connman using Qt


%package tests
Summary:    Tests for connman-qt
Group:      Development/Libraries
Requires:   %{name} = %{version}-%{release}
Requires:   connman-qt-declarative

%description tests
This package contains the test applications for testing libconnman-qt


%package declarative
Summary:    Declarative plugin for Qt Quick for connman-qt
Group:      Development/Libraries
Requires:   %{name} = %{version}-%{release}
Requires:   connman-qt

%description declarative
This package contains the files necessary to develop
applications using libconnman-qt


%package devel
Summary:    Development files for connman-qt
Group:      Development/Libraries
Requires:   %{name} = %{version}-%{release}

%description devel
This package contains the files necessary to develop
applications using libconnman-qt


%prep
%setup -q -n %{name}-%{version}

%build
export PATH=$PATH:/usr/lib/qt4/bin
qmake install_prefix=/usr

%qmake -r VERSION=%{version}

make %{?_smp_mflags}

%install
rm -rf %{buildroot}
export INSTALL_ROOT=%{buildroot}
%qmake_install

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%{_libdir}/libconnman-qt4.so.*

%files tests
%defattr(-,root,root,-)
/opt

%files declarative
%defattr(-,root,root,-)
%{_usr}/lib/qt4/imports/MeeGo/Connman

%files devel
%defattr(-,root,root,-)
%{_usr}/include/connman-qt4
%{_usr}/lib/pkgconfig/connman-qt4.pc
%{_usr}/lib/libconnman-qt4.prl
%{_usr}/lib/libconnman-qt4.so
