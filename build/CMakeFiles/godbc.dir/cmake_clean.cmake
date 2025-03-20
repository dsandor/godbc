file(REMOVE_RECURSE
  "lib/libgodbc.a"
  "lib/libgodbc.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang )
  include(CMakeFiles/godbc.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
