# LaTeX paper. Off by default; enable with -DCTPB_BUILD_PAPER=ON.

find_package(LATEX REQUIRED)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/paper.pdf
    COMMAND pdflatex -interaction=nonstopmode main.tex
    COMMAND bibtex main
    COMMAND pdflatex -interaction=nonstopmode main.tex
    COMMAND pdflatex -interaction=nonstopmode main.tex
    COMMAND ${CMAKE_COMMAND} -E copy main.pdf ${CMAKE_CURRENT_BINARY_DIR}/paper.pdf
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/main.tex
            ${CMAKE_CURRENT_SOURCE_DIR}/bibliography.bib
            ${CMAKE_CURRENT_SOURCE_DIR}/sections/introduction.tex
            ${CMAKE_CURRENT_SOURCE_DIR}/sections/background.tex
            ${CMAKE_CURRENT_SOURCE_DIR}/sections/architecture.tex
            ${CMAKE_CURRENT_SOURCE_DIR}/sections/constexpr_parser.tex
            ${CMAKE_CURRENT_SOURCE_DIR}/sections/static_reflection.tex
            ${CMAKE_CURRENT_SOURCE_DIR}/sections/wire_engine.tex
            ${CMAKE_CURRENT_SOURCE_DIR}/sections/benchmark.tex
            ${CMAKE_CURRENT_SOURCE_DIR}/sections/related_work.tex
            ${CMAKE_CURRENT_SOURCE_DIR}/sections/conclusion.tex
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
add_custom_target(paper DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/paper.pdf)

