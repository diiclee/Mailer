If not already done, please install:
sudo apt install libldap2-dev

After that you open 2 terminals:

In the first start the server:
./twmailer-server <port> <mails folder>

and in the second you start the client:
./twmailer-client 127.0.0.1 <port>

and now you can use the commands ^^