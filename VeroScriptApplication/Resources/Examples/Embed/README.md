# Native SDK consumer

Configure this example with `-DCMAKE_PREFIX_PATH=/absolute/sdk`, build, then run `application-consumer`. It uses public installed headers and exported targets only. It constructs and runs an application in memory, and links Build and the read-only PackageRuntime without the HTTP/install client. C++ Session conveniences are available separately in the C++ SDK; this example uses Core's VM directly.
