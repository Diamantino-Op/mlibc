sysdep_supported_options = {
    'posix': false,
    'linux': false,
    'glibc': false,
    'bsd': false,
}

rtld_sources += files(
    'generic/horizonos.cpp',
    'generic/generic.cpp',
)
rtld_include_dirs += include_directories('include')

libc_sources += files(
    'generic/horizonos.cpp',
    'generic/generic.cpp',
    'generic/entry.cpp',
)
libc_include_dirs += include_directories('include')
