./scripts/prerebuild.sh ;
git pull origin dev \
&& \
docker build -t weyoume/wenode . ;