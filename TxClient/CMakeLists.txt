add_executable(TxClient
    main.cpp
    src/client_console.cpp
)

target_include_directories(TxClient PRIVATE include)
target_link_libraries(TxClient PRIVATE shared)
