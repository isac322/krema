import socket, os

path = f"/run/user/{os.getuid()}/hypr/{os.environ.get('HYPRLAND_INSTANCE_SIGNATURE')}/.socket.sock"
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(path)
s.sendall(b"/dispatch hl.dsp.focus({window=\"address:0x1a49992a400\", follow=false})\n")
print("Response:", s.recv(1024))
