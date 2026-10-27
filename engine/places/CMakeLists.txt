# The place ontology, the constraint expression language and the validators
# of the building documents (development_plan.md 7.1).
add_library(aige_places STATIC
    Ontology.h
    Ontology.cpp
    OntologyExport.h
    OntologyExport.cpp
    OntologyEdit.h
    OntologyEdit.cpp
    Expr.h
    Expr.cpp
    PlaceValidate.h
    PlaceValidate.cpp
    Brief.h
    Brief.cpp
    Planner.h
    Planner.cpp
    ReviseLanguage.cpp
    Solver.h
    Solver.cpp
    Where.h
    Where.cpp
    Compile.h
    Compile.cpp
    Arrange.h
    Arrange.cpp
)
target_include_directories(aige_places PUBLIC ${CMAKE_SOURCE_DIR}/engine)
target_link_libraries(aige_places PUBLIC nlohmann_json::nlohmann_json)
target_compile_definitions(aige_places PRIVATE AIGE_PLACES_DATA_DIR="${CMAKE_SOURCE_DIR}/data/places")
target_compile_features(aige_places PUBLIC cxx_std_20)
