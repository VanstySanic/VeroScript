# Loopback TLS fixture

The certificate and its matching, public test key are generated fixtures for the
local HTTPS test server. They identify `localhost` and `127.0.0.1`, with validity
from 2020 through 2120 so the tests do not depend on their generation date.

The client uses this certificate as its explicit CA file for that request. The
test runner does not modify the operating system's certificate store. These
fixtures are excluded from product payloads with the rest of `Resources/Tests`.
