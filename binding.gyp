{
  'targets': [
    {
      'target_name': 'sword-addon',
      'sources': [ 'sword-addon.cc' ],
      'conditions': [
        ['OS=="linux"',
          {
            'link_settings': {
              'libraries': [
                '-lsword -lz -lcurl'
              ],
            },
            'include_dirs': [
              '/usr/include/sword',
              'include'
            ],
          }
        ],
      ]
    }
  ]
}