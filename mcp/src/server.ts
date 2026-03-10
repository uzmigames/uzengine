/// UzEngine MCP (Model Context Protocol) server.
///
/// Implements the MCP server interface for Claude integration, enabling
/// LLM access to UzEngine runtime capabilities via stdio transport.

import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { mcpLog } from './logger.js';

/// Creates the MCP server instance for UzEngine.
const server = new McpServer({
  name: 'uzengine-mcp',
  version: '0.2.0',
});

/// Initializes and runs the MCP server.
///
/// # Behavior
/// - Creates a stdio transport for protocol communication
/// - Connects the server to the transport
/// - Logs connection status
/// - Keeps the process running until shutdown
///
/// # Errors
/// If connection fails, logs a fatal error and exits with code 1.
async function main(): Promise<void> {
  mcpLog.info('UzEngine MCP server starting');
  const transport = new StdioServerTransport();
  await server.connect(transport);
  mcpLog.info('UzEngine MCP server connected');
}

/// Entry point: starts the MCP server.
///
/// Catches any fatal errors during initialization and gracefully exits.
main().catch((err) => {
  mcpLog.fatal({ err }, 'MCP server fatal error');
  process.exit(1);
});
