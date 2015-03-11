-- bcc compiler.

solution 'bcc'
   configurations 'release'
   language 'C'
   buildoptions {
      '-Wall',
      '-Werror',
      '-Wno-error=switch',
      '-Wno-unused',
      '-std=c99',
      '-pedantic',
      '-Wstrict-aliasing',
      '-Wstrict-aliasing=2',
      '-D_BSD_SOURCE',
   }
   flags {
      'Symbols',
   }
   includedirs {
      'src/',
   }

   project 'src'
      location 'build/src'
      kind 'ConsoleApp'
      targetname 'bcc'
      files {
         'src/*.c',
      }
      links {
         'src_codegen',
         'src_parse'
      }

   project 'src_codegen'
      location 'build/src_codegen'
      targetdir 'build/src_codegen'
      kind 'StaticLib'
      files {
         'src/codegen/*.c'
      }

   project 'src_parse'
      location 'build/src_parse'
      targetdir 'build/src_parse'
      kind 'StaticLib'
      files {
         'src/parse/*.c'
      }