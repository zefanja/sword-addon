{
  'targets': [
    {
      'target_name': 'sword',
      'sources': [ 'sword.cc' ],
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
            ],
          }
        ],
      ]
    }
  ]
}