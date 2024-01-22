#!/bin/bash

PROXY_ADDRESS="127.0.0.1"
PROXY_PORT="1080"
URL_DOMAIN_NAME="ifconfig.me"
URL_IPV4="127.0.0.1:10000"

TESTCASE[0]="curl -x socks4://$PROXY_ADDRESS:$PROXY_PORT $URL_DOMAIN_NAME"
TESTCASE[1]="curl -x socks4://$PROXY_ADDRESS:$PROXY_PORT $URL_IPV4"
TESTCASE[2]="curl -x socks5://$PROXY_ADDRESS:$PROXY_PORT $URL_DOMAIN_NAME"
TESTCASE[3]="curl -x socks5://$PROXY_ADDRESS:$PROXY_PORT $URL_IPV4"

for ((i = 0; i < ${#TESTCASE[@]}; i++)) ; do
    t="${TESTCASE[$i]}"; echo "====="; echo "test case: \`$t\`"; eval "$t"; echo; echo "====="; echo;
done