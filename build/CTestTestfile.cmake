# CMake generated Testfile for 
# Source directory: /Users/user/KVM/OpenVMS-8.4/COAB/coab
# Build directory: /Users/user/KVM/OpenVMS-8.4/COAB/coab/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(selftest "/Users/user/KVM/OpenVMS-8.4/COAB/coab/build/coab" "--self-test" "--out" "/Users/user/KVM/OpenVMS-8.4/COAB/coab/build/selftest-out")
set_tests_properties(selftest PROPERTIES  FAIL_REGULAR_EXPRESSION "FAIL" WORKING_DIRECTORY "/Users/user/KVM/OpenVMS-8.4/COAB/coab" _BACKTRACE_TRIPLES "/Users/user/KVM/OpenVMS-8.4/COAB/coab/CMakeLists.txt;451;add_test;/Users/user/KVM/OpenVMS-8.4/COAB/coab/CMakeLists.txt;0;")
