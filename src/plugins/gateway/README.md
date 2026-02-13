# OpenCrank Gateway Plugin for C++

## Overview

This plugin implements a WebSocket-based gateway server compatible with the OpenCrank gateway protocol. It allows remote control and monitoring of the OpenCrank C++ agent framework.

## Implementation Details

### Architecture

The gateway plugin is built using:
- **Crow** (https://crowcpp.org) - Header-only C++ web framework for HTTP and WebSocket
- **OpenCrank plugin API** for integration with the framework
- **JSON-RPC-like protocol** for client-server communication

### Dependencies

- **Crow** is included as a header-only dependency in `deps/crow_all.h`
- No external library dependencies (just system libraries: pthread, zlib)
- Requires C++17 (only for this plugin, rest of codebase uses C++)

### Key Components

1. **GatewayPlugin** (`gateway.cpp`, `gateway.hpp`)
   - Main plugin class that implements the OpenCrank Plugin interface
   - Manages HTTP/WebSocket server lifecycle
   - Handles protocol message routing

2. **WebSocketServer** (internal class)
   - Wraps Crow HTTP and WebSocket functionality
   - Manages client connections
   - Handles HTTP routes and WebSocket events

3. **GatewayClient** 
   - Represents a connected client
   - Tracks authentication state
   - Manages protocol version negotiation

### Protocol Support

The gateway implements the following OpenCrank protocol methods:

- **hello** - Initial handshake and protocol negotiation
- **chat.send** - Send chat messages to registered channel plugins
- **config.get** - Get gateway configuration
- **health.get** - Get gateway health status
- **models.list** - List available AI models

Events supported:
- **chat.message** - Incoming messages from channels (full duplex)
- **chat.delta** - Streaming chat responses
- **chat.done** - Chat completion
- **heartbeat** - Keep-alive messages

### Endpoints

- `GET /` - Serves the Control UI (HTML)
- `GET /index.html` - Same as above
- `WS /ws` - WebSocket endpoint for gateway protocol

## Configuration

Add to your `config.json`:

```json
{
  "plugins": ["gateway"],
  "gateway": {
    "port": 18789,
    "bind": "127.0.0.1",
    "auth": {
      "token": "your-secret-token"
    }
  }
}
```

### Configuration Options

- `gateway.port` (int, default: 18789) - WebSocket server port
- `gateway.bind` (string, default: "127.0.0.1") - Bind address
- `gateway.auth.token` (string, optional) - Authentication token

## Building

### Prerequisites

```shell
# Install libwebsockets development package
# On Fedora/RHEL:
sudo dnf install libwebsockets-devel

# On Debian/Ubuntu:
sudo apt-get install libwebsockets-dev
```

### Build Commands

```shell
cd src/plugins/gateway
make
make install
```

This will create `bin/plugins/gateway.so`.

## Usage

### Starting the Gateway

The gateway automatically starts when the plugin is loaded and initialized:

```shell
./bin/opencrank config.json
```

If the gateway plugin is configured, you'll see:

```
[INFO] Initializing gateway plugin...
[INFO] Gateway started on ws://127.0.0.1:18789
[INFO] Gateway plugin initialized and started (port: 18789)
```

### Testing with WebSocket Client

Using `wscat`:

```shell
npm install -g wscat
wscat -c ws://localhost:18789
```

Then send a hello message:

```json
{
  "type": "hello",
  "params": {
    "minProtocol": 1,
    "maxProtocol": 1,
    "client": {
      "id": "test-client",
      "version": "1.0.0",
      "platform": "test",
      "mode": "cli"
    },
    "auth": {
      "token": "your-secret-token"
    }
  }
}
```

### Testing Health Endpoint

```json
{
  "type": "call",
  "method": "health.get",
  "params": {},
  "id": "req-1"
}
```

Response:

```json
{
  "type": "result",
  "id": "req-1",
  "result": {
    "status": "ok",
    "clients": 1,
    "port": 18789
  }
}
```

## Integration with OpenCrank Hub

This C++ gateway implementation is designed to be compatible with the OpenCrank TypeScript gateway protocol, allowing it to integrate with:

- OpenCrank CLI clients
- OpenCrank mobile apps
- OpenCrank web UI
- Custom gateway clients

## Comparison with TypeScript Implementation

### Similarities

- Same WebSocket protocol
- Same message format (JSON-RPC-like)
- Compatible authentication mechanism
- Support for protocol version negotiation

### Differences

- **Language**: C++ vs TypeScript/Node.js
- **WebSocket Library**: libwebsockets vs ws npm package
- **Performance**: Lower memory footprint, faster startup
- **Deployment**: Single binary vs Node.js runtime required

### Located TypeScript Implementation

The gateway implementation is based on analyzing the OpenCrank TypeScript source:

- `src/cli/gateway-cli/run.ts` - CLI command for starting gateway
- `src/gateway/server.impl.ts` - Main server implementation
- `src/gateway/server-ws-runtime.ts` - WebSocket runtime
- `src/gateway/protocol/schema/frames.ts` - Protocol definitions
- `src/gateway/server-methods.ts` - RPC method handlers

## Future Enhancements

1. **Chat Integration**: Connect to AI plugins for full chat.send support
2. **Session Management**: Track and manage chat sessions
3. **Model Catalog**: Dynamically list available AI models
4. **Configuration API**: Allow runtime configuration updates
5. **HTTP Endpoints**: Add REST API for non-WebSocket clients
6. **TLS Support**: Secure WebSocket connections (wss://)
7. **Hooks**: Support webhook-style HTTP POST endpoints
8. **Broadcasting**: Implement server-side event broadcasting

## Files

```
include/opencrank/plugins/gateway/
  gateway.hpp          - Plugin header

src/plugins/gateway/
  gateway.cpp          - Plugin implementation
  Makefile             - Build configuration

bin/plugins/
  gateway.so           - Compiled plugin
```

## License

Same as OpenCrank framework (MIT).
