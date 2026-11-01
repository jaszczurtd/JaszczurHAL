# A Git repository for `jh repository files`: a tracked, an untracked and an
# ignored file, names with a space and a non-ASCII letter, and Git's own
# NUL-separated listing of the tracked and untracked files.
# jh_repository_fixture(<git> <directory> <listing file>)
function(jh_repository_fixture git directory listing)
  file(MAKE_DIRECTORY "${directory}")
  execute_process(COMMAND "${git}" init -q "${directory}" COMMAND_ERROR_IS_FATAL ANY)
  file(WRITE "${directory}/.gitignore" "ignored\n")
  file(WRITE "${directory}/tracked ż" "tracked")
  execute_process(COMMAND "${git}" -C "${directory}" add .gitignore "tracked ż"
    COMMAND_ERROR_IS_FATAL ANY)
  file(WRITE "${directory}/untracked file" "untracked")
  file(WRITE "${directory}/ignored" "ignored")
  execute_process(COMMAND "${git}" -C "${directory}" ls-files --cached --others --exclude-standard -z
    OUTPUT_FILE "${listing}" COMMAND_ERROR_IS_FATAL ANY)
endfunction()

# Fail unless <output file> holds exactly the bytes of <listing file>.
function(jh_expect_listing listing output message)
  file(SHA256 "${listing}" expected)
  file(SHA256 "${output}" actual)
  if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "${message}")
  endif()
endfunction()
