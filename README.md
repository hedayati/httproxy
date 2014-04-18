How to make the code?
The code comes with a Makefile that can be used to build the executable
using:
# make

------------------------------------------------------------------------

How to run the server?
After making the code, you will have a executable called "proxy" in the
same directory as your source. To start the server, run:
# ./proxy 8080

where, 8080 is the port number on which the proxy server will listen. You
can use any other (over 1024) number as port number value.

------------------------------------------------------------------------

How to use the proxy in my browser?
The recommanded way of using this proxy is by changing http proxy settings
in your browser. Use the server's ip address with the given port number in
the setting fields.

The other way of using this proxy is by accessing the website (e.g.
www.somesite.com/site/index.html), directly using the following URL in the
address bar:

http://<proxy-ip>:<proxy-port>/www.somesite.com/site/index.html

e.g.
	http://192.168.1.1:8080/www.somesite.com/site/index.html
	http://10.0.0.12:8888/http://www.somesite.com/site/index.html

Please note that the second approach may cause some problems as the
original server sends redirect responses (e.g. 301, moved permenantly)
since the browser will try to fetch the redirect address, directly and not
through the proxy server.

------------------------------------------------------------------------

How to test the URL blocking?
Line 21 of the source code (proxy.c) comes with a list of blacklisted
URLs. Any URL that has any of these words as a part will be blocked (see
Line 166).

So, in order to test this feature, navigate to (change the ip:port as
necessary):
http://10.0.0.12:8888/www.facebook.com/

or directly enter http://www.hulu.com/ if you have changed your browser
settings.

------------------------------------------------------------------------

How to test the content filtering?
Line 26 of the source code (proxy.c) comes with a list of blacklisted
words. Any content that has any of these words will be blocked (see
Line 353).

In order to test this feature, navigate to (change the ip:port as
necessary):
http://localhost:8080/http://www.cs.berkeley.edu/~vazirani/algorithms/

The word "algorithm", which is a blacklisted word, can be found on this
page.

------------------------------------------------------------------------

How to test the caching?
All visited contents will be cached in ./cache/ directory with a name
same as their URL.

To test caching try to navigate to:
http://localhost:8080/http://www-inst.eecs.berkeley.edu/images/iesg2.jpg
and see the output of the server in console. Somewhere in debugging info,
it says:
-> connected to host: www-inst.eecs.berkeley.edu w/ ip: 128.32.42.199
-> first access...
-> response_code: 200
-> from remote host... 

Now try to access the same url again, this time the output will be like:
-> connected to host: www-inst.eecs.berkeley.edu w/ ip: 128.32.42.199
-> conditional GET...
-> If-Modified-Since: Fri, 18 Apr 2014 17:12:59 GMT
-> response_code: 304
-> not modified
-> from local cache...
