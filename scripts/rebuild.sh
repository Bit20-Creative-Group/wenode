./scripts/prerebuild.sh \
&& \
git pull origin dev \
&& \
docker build -t lopudesigns/ezira-test-node . ;