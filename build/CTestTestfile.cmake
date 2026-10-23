# CMake generated Testfile for 
# Source directory: C:/Users/Lenovo/Doubao/chats/2026-09-10/new-chat-2/Epoll-Reactor-TCP-Server
# Build directory: C:/Users/Lenovo/Doubao/chats/2026-09-10/new-chat-2/Epoll-Reactor-TCP-Server/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test(protocol_test "C:/Users/Lenovo/Doubao/chats/2026-09-10/new-chat-2/Epoll-Reactor-TCP-Server/build/Debug/protocol_test.exe")
  set_tests_properties(protocol_test PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/Lenovo/Doubao/chats/2026-09-10/new-chat-2/Epoll-Reactor-TCP-Server/CMakeLists.txt;38;add_test;C:/Users/Lenovo/Doubao/chats/2026-09-10/new-chat-2/Epoll-Reactor-TCP-Server/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test(protocol_test "C:/Users/Lenovo/Doubao/chats/2026-09-10/new-chat-2/Epoll-Reactor-TCP-Server/build/Release/protocol_test.exe")
  set_tests_properties(protocol_test PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/Lenovo/Doubao/chats/2026-09-10/new-chat-2/Epoll-Reactor-TCP-Server/CMakeLists.txt;38;add_test;C:/Users/Lenovo/Doubao/chats/2026-09-10/new-chat-2/Epoll-Reactor-TCP-Server/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test(protocol_test "C:/Users/Lenovo/Doubao/chats/2026-09-10/new-chat-2/Epoll-Reactor-TCP-Server/build/MinSizeRel/protocol_test.exe")
  set_tests_properties(protocol_test PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/Lenovo/Doubao/chats/2026-09-10/new-chat-2/Epoll-Reactor-TCP-Server/CMakeLists.txt;38;add_test;C:/Users/Lenovo/Doubao/chats/2026-09-10/new-chat-2/Epoll-Reactor-TCP-Server/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test(protocol_test "C:/Users/Lenovo/Doubao/chats/2026-09-10/new-chat-2/Epoll-Reactor-TCP-Server/build/RelWithDebInfo/protocol_test.exe")
  set_tests_properties(protocol_test PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/Lenovo/Doubao/chats/2026-09-10/new-chat-2/Epoll-Reactor-TCP-Server/CMakeLists.txt;38;add_test;C:/Users/Lenovo/Doubao/chats/2026-09-10/new-chat-2/Epoll-Reactor-TCP-Server/CMakeLists.txt;0;")
else()
  add_test(protocol_test NOT_AVAILABLE)
endif()
