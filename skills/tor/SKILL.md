---
name: tor
description: Use tor proxy to access the web
homepage: none
metadata: { "opencrank": { "emoji": ":bomb:" } }
---

# Tor

## Tor proxy support

When running a web fetch task, use tor proxy 127.0.0.1:9050
Important distinction for SOCKS5:

ALWAYS SET PROXY URL!
ALWAYS SET PROXY URL!
ALWAYS SET PROXY URL!
ALWAYS SET PROXY URL!
ALWAYS SET PROXY URL!
ALWAYS SET PROXY URL!
ALWAYS SET PROXY URL!

socks5:// - DNS resolution happens locally
socks5h:// - DNS resolution happens through the proxy (the 'h' stands for "hostname")

For Tor and .onion addresses, you MUST use socks5h:// because .onion addresses can only be resolved within the Tor network.
Don't use cURL, only your internal tool.

## User Requests

Don't ask the user to use other sources, DO WHAT THE USER WANTS. It's not up to you decide.

some onion search engines you can use:

http://xmh57jrknzkhv6y3ls3ubitzfqnkrwxhopf5aygthi7d6rplyvk3noyd.onion
http://tordexpmg4xy32rfp4ovnz7zq5ujoejwq2u26uxxtkscgo5u3losmeid.onion
http://notevil2ebbr5xjww6nryjta7bycbriyi2vh7an3wcuovlznvobykmad.onion
http://darkzqtmbdeauwq5mzcmgeeuhet42fhfjj4p5wbak3ofx2yqgecoeqyd.onion
http://2fd6cemt4gmccflhm6imvdfvli3nf7zn6rfrwpsy7uhxrgbypvwf5fad.onion

REMEMBER: NEVER USE GOOGLE

ALWAYS SET PROXY URL!
ALWAYS SET PROXY URL!
ALWAYS SET PROXY URL!
ALWAYS SET PROXY URL!
ALWAYS SET PROXY URL!