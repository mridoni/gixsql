Name:           gixsql
Version:        #GIXSQLMAJ#.#GIXSQLMIN#.#GIXSQLREL#
Release:        #PKGVER#%{?dist}
Summary:        GixSQL is an ESQL preprocessor for GnuCOBOL

License:        GPL-3.0-or-later AND LGPL-3.0-or-later
URL:            https://github.com/mridoni/gixsql
Source0:        gixsql-#GIXSQLMAJ#.#GIXSQLMIN#.#GIXSQLREL#.tar.gz

BuildRequires:  gcc gcc-c++ make automake autoconf libtool pkg-config postgresql-devel mariadb-devel unixODBC-devel flex spdlog-devel fmt-devel git wget diffutils
Requires:       postgresql-devel mariadb-devel unixODBC spdlog fmt

%description
GixSQL is an ESQL preprocessor and a series of runtime libraries to enable GnuCOBOL to access ODBC, MySQL, PostgreSQL, Oracle and SQLite databases

%prep
%autosetup


%build
chmod 755 $RPM_BUILD_ROOT/prepbuild.sh && $RPM_BUILD_ROOT/prepbuild.sh
chmod 755 $RPM_BUILD_ROOT/prepdist.sh && $RPM_BUILD_ROOT/prepdist.sh
%configure
%make_build CXXFLAGS="-DNDEBUG"


%install
rm -rf $RPM_BUILD_ROOT
%make_install


%files
%license $RPM_BUILD_ROOT/usr/share/gixsql/doc/LICENSE
%doc $RPM_BUILD_ROOT/usr/share/gixsql/doc/README
%defattr(-,root,root,-)
/usr/bin/*
/usr/include/*
/usr/lib/*
/usr/lib64/*
/usr/share/gixsql/*

%changelog
* Thu May 18 2023 root
-
