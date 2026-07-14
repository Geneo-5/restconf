echo "=============== build/test --jwt-off and --plugin-off"
echo "=============== build/test --jwt-off and --plugin-off" > build.log
./scripts/build_test.sh $@ >>build.log 2>&1
echo "=============== build/test --jwt-off and --plugin-on"
echo "=============== build/test --jwt-off and --plugin-on" >> build.log
./scripts/build_test.sh --plugin-on $@ >>build.log 2>&1
