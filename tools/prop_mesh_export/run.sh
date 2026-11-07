#!/bin/sh
# usage: run.sh [kind=resource ...]   (env Q=regex lists names; CAT=name dumps a resource as text)
cd "$(dirname "$0")" && dotnet bin/Release/net8.0-windows/prop_mesh_export.dll "C:/Program Files (x86)/Steam/steamapps/common/Dishonored2" ../../artifacts/prop-meshes "$@" </dev/null
