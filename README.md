# xdccget
A minimal, secure, command-line tool for interfacing with XDCC.

#### Supported Operating Systems:
* GNU/Linux
* OpenBSD
* FreeBSD
* macOS

## Using xdccget
In order to use xdccget properly i will provide some simple examples. You should be able to extract 
the options for your personal usage quite quickly, i guess:

For example, let's assume that you want to download the package *34* from the bot *super-duper-bot*
at the channel *best-channel* from the irc-server
*irc.sampel.net* without ssl and from the standard-irc-port *6667*. 
Then the command line argument for xdccget would be:

``` 
xdccget irc://irc.sampel.net/#best-channel super-duper-bot send 34
``` 

This would download the package *34* from *super-duper-bot* without using ssl. You can also specifiy a 
special port, so lets assume that the *irc.sampel.net* server would use the port 1337. Then our xdcc-get-call would
be like this:

``` 
xdccget irc://irc.sampel.net:1337/#best-channel super-duper-bot send 34
``` 

If your irc-network supports ssl you can even use an secure ssl-connection with xdccget. So lets imagine that 
*irc.sampel.net* uses ssl on port 1338. Then we would call xdccget like this to use ssl:

``` 
xdccget ircs://irc.sampel.net:1338/#best-channel super-duper-bot send 34
``` 

If the bot even supports ssl than you can use the ssend-command to use an ssl-encrypted connection with the bot.
So for example if the *super-duper-bot* would support ssl-connection, then we could call xdccget like:

``` 
xdccget ircs://irc.sampel.net:1338/#best-channel super-duper-bot ssend 34
``` 

Notice the *xdcc ssend* command instead of *xdcc send*. This tells the bot that we want connect to him with ssl 
enabled.

You can also join multiple channels, so if you also have to join #best-chat-channel in order to download packages from #best-channel, then you can call xdccget like:

``` 
xdccget irc://irc.sampel.net/#best-channel,#best-chat-channel super-duper-bot send 34
``` 

This is the basic usage of xdccget. You can call xdccget -h to understand all currently supported arguments.

## Compiling xdccget

If compiling for GNU/Linux, you'll need the compile-time dependency _libbsd_ (or _libbsd-dev_). 

```shell
meson setup build
ninja -C build
```
---

[![C/C++ CI](https://github.com/mario-campos/xdccget/actions/workflows/c-cpp.yml/badge.svg)](https://github.com/mario-campos/xdccget/actions/workflows/c-cpp.yml)
