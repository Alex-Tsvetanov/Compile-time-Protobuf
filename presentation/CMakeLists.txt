# LaTeX Presentation Target
find_package(LATEX REQUIRED)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/presentation.pdf
    COMMAND pdflatex -interaction=nonstopmode presentation.tex
    COMMAND pdflatex -interaction=nonstopmode presentation.tex
    COMMAND ${CMAKE_COMMAND} -E copy presentation.pdf ${CMAKE_CURRENT_BINARY_DIR}/presentation.pdf
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presentation.tex
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_custom_target(presentation DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/presentation.pdf)
