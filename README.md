### WM

This is just a draft of a working "port" of my window manager. It's a copy of the Inferno OS window manager, now for Linux.

> There are no guarantees that it will work for you.

```javascript
sudo apt update && sudo apt install build-essential pkg-config libx11-dev libcairo2-dev libfontconfig1-dev

cp inferno_wm.conf-example ~/.inferno_wm.conf

make

make install
```
