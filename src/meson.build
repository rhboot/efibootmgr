con2 = configuration_data()
con2.set_quoted('DEFAULT_LOADER', default_loader)
configure_file(
  input: 'efibootmgr.8.in',
  output: 'efibootmgr.8',
  configuration: con2,
  install: true,
  install_dir: datadir / 'man/man8',
)
configure_file(
  input: 'efibootdump.8.in',
  output: 'efibootdump.8',
  configuration: con2,
  install: true,
  install_dir: datadir / 'man/man8',
)

executable(
  'efibootmgr',
  sources: [
    'efibootmgr.c',
    'efi.c',
    'parse_loader_data.c',
  ],
  include_directories: [
    root_incdir,
  ],
  dependencies: [
    efivar,
    efiboot,
    popt,
  ],
  install: true,
  install_dir: sbindir,
)

executable(
  'efibootdump',
  sources: [
    'efibootdump.c',
    'parse_loader_data.c',
  ],
  include_directories: [
    root_incdir,
  ],
  dependencies: [
    efivar,
    efiboot,
    popt,
  ],
  install: true,
  install_dir: sbindir,
)

executable(
  'eficonman',
  sources: [
    'eficonman.c',
  ],
  include_directories: [
    root_incdir,
  ],
  dependencies: [
    efivar,
    popt,
  ],
)
