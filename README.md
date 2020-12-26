# xdccget
This is a project that allows you to download files from IRC with XDCC with an easy and simple to use command line tool
like wget or curl. It supports at the moment Linux and BSD-variants. Also OSX with some installed
ports works.

## Quick facts
* it is free software licenced under the GPL
* minimal usage of cpu and memory
* runs under Linux, BSDs, MacOSX
* support for IPv4 and IPv6 connections
* supports connection with and without SSL/TLS
* bots with support for ssend-command are supported

## Using xdccget
In order to use xdccget properly i will provide some simple examples. You should be able to extract 
the options for your personal usage quite quickly, i guess:

For example, let's assume that you want to download the package *34* from the bot *super-duper-bot*
at the channel *best-channel* from the irc-server
*irc.sampel.net* without ssl and from the standard-irc-port *6667*. 
Then the command line argument for xdccget would be:

``` 
xdccget "irc.sampel.net" "#best-channel" "super-duper-bot xdcc send #34"
``` 

This would download the package *34* from *super-duper-bot* without using ssl. You can also specifiy a 
special port, so lets assume that the *irc.sampel.net* server would use the port 1337. Then our xdcc-get-call would
be like this:

``` 
xdccget -p 1337 "irc.sampel.net" "#best-channel" "super-duper-bot xdcc send #34"
``` 

If your irc-network supports ssl you can even use an secure ssl-connection with xdccget. So lets imagine that 
*irc.sampel.net* uses ssl on port 1338. Then we would call xdccget like this to use ssl:

``` 
xdccget -p 1338 "#irc.sampel.net" "#best-channel" "super-duper-bot xdcc send #34"
``` 

Notice the #-character in front of irc.sampel.net. This tells xdccget to use ssl/tls on the connection to the irc-server.
If the bot even supports ssl than you can use the ssend-command to use an ssl-encrypted connection with the bot.
So for example if the *super-duper-bot* would support ssl-connection, then we could call xdccget like:

``` 
xdccget -p 1338 "#irc.sampel.net" "#best-channel" "super-duper-bot xdcc ssend #34"
``` 

Notice the *xdcc ssend* command instead of *xdcc send*. This tells the bot that we want connect to him with ssl 
enabled.

You can also join multiple channels, so if you also have to join #best-chat-channel in order to download packages from #best-channel, then you can call xdccget like:

``` 
xdccget "irc.sampel.net" "#best-channel, #best-chat-channel" "super-duper-bot xdcc send #34"
``` 

This is the basic usage of xdccget. You can call xdccget -h to understand all currently supported arguments.

## Compiling xdccget
Compiling xdccget is just running make from the root folder of the repository. Please make sure, that you have installed
the depended libraries (OpenSSL) and use the correct Makefile for your system.

### Ubuntu and derivants
To compile xdccget under Ubuntu and other distros like Linux Mint you have to install the package libssl-dev with apt-get.
You also need the build-essential package. 

```
sudo apt-get install libssl-dev build-essential
```

### other linux distros
You need to make sure, that you have the openssl-development packages for you favorite distribution installed.

### OSX and BSD
For osx and bsd systems you need to also install the development files for openssl.
