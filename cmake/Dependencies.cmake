# Third-party dependencies, pinned by version and hash. Release tarballs are
# downloaded instead of cloned: much faster, and the hash makes them reproducible.
# NL2X_DOWNLOAD_CACHE keeps the tarballs in a directory that survives `rm -rf build` (CI caches
# it); a tarball already there with the right hash is not downloaded again.

set(NL2X_DOWNLOAD_CACHE "" CACHE PATH "Optional directory where dependency tarballs are kept")
set(nl2x_download_dir "")
if(NL2X_DOWNLOAD_CACHE)
    set(nl2x_download_dir DOWNLOAD_DIR "${NL2X_DOWNLOAD_CACHE}")
endif()

if(NL2X_BUILD_TESTS)
    FetchContent_Declare(Catch2
        URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.9.1.tar.gz
        URL_HASH SHA256=a215c2a723bd7483efd236dc86066842a389cb4e344c61119c978acdf24d39be
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        ${nl2x_download_dir})
    FetchContent_MakeAvailable(Catch2)
endif()

if(NL2X_BUILD_PLUGIN)
    FetchContent_Declare(JUCE
        URL https://github.com/juce-framework/JUCE/archive/refs/tags/9.0.4.tar.gz
        URL_HASH SHA256=e2bee25d5c776118aea550ad168a3dc9520c0669a79bd9f5a72b9f404ee1c40a
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        ${nl2x_download_dir})
    FetchContent_MakeAvailable(JUCE)
endif()
