project('efibootmgr', 'c',
  version: '18',
  license: 'GPL-2.1+',
  meson_version: '>=0.61.0',
  default_options: ['warning_level=2', 'c_std=c11'],
)

warning_flags = [
  '-Wno-unused-parameter'
]
cc = meson.get_compiler('c')
add_project_arguments(cc.get_supported_arguments(warning_flags), language: 'c')

add_project_arguments('-D_GNU_SOURCE', language: 'c')

prefix = get_option('prefix')
sbindir = prefix / get_option('sbindir')
libdir = join_paths(prefix, get_option('libdir'))
datadir = prefix / get_option('datadir')
localedir = prefix / get_option('localedir')

root_incdir = include_directories('.')

efivar = dependency('efivar')
efiboot = dependency('efiboot')
popt = dependency('popt')

conf = configuration_data()
conf.set_quoted('LOCALEDIR', localedir)
conf.set_quoted('LIBDIR', libdir)
conf.set_quoted('EFIBOOTMGR_VERSION', meson.project_version())
conf.set_quoted('EFIDIR', get_option('efidir'))
conf.set_quoted('EFI_LABEL', get_option('efi_label'))
conf.set_quoted('EFI_LOADER', get_option('efi_loader'))

default_loader = '\\\\EFI\\\\@0@\\\\@1@'.format(get_option('efidir'), get_option('efi_loader'))
conf.set_quoted('DEFAULT_LOADER', default_loader)

configure_file(
  output: 'config.h',
  configuration: conf
)

con2 = configuration_data()
con2.set('VERSION', meson.project_version())
configure_file(
  input: 'efibootmgr.spec.in',
  output: 'efibootmgr.spec',
  configuration: con2,
)

subdir('src')
