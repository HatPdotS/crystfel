"""
    CrystFEL

Julia bindings for CrystFEL data structures and routines

## Quick start
```julia
  using CrystFEL
  ...
```
"""
module CrystFEL

libcrystfel = "libcrystfel.so"

include("cell.jl")
using .UnitCells
export UnitCell, LatticeType
export TriclinicLattice, MonoclinicLattice, OrthorhombicLattice
export TetragonalLattice, HexagonalLattice, RhombohedralLattice, CubicLattice

include("detgeom.jl")
using .DetGeoms
export Panel, DetGeom

include("symmetry.jl")
using .Symmetry
export SymOpList

include("datatemplates.jl")
using .DataTemplates
export DataTemplate, loaddatatemplate

include("image.jl")
using .Images
export Image

include("reflists.jl")
using .RefLists
export RefList, Reflection, loadreflist, savereflections

end  # of module
