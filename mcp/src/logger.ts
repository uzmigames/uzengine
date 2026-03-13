/// Logger for the UzEngine MCP server.
///
/// Provides structured logging via Pino to stderr (file descriptor 2).
/// This separation is critical because the MCP stdio transport uses stdout for protocol messages.

import pino from 'pino';

/// MCP server logger instance.
///
/// # CRITICAL: Never use console.log()
/// The MCP server communicates via stdio transport. Using console.log() corrupts stdout
/// and breaks the protocol communication. All logging must go to stderr via this logger.
///
/// # Configuration
/// - Transport: Pino file transport directed to stderr (fd 2)
/// - Level: Configurable via `UZ_MCP_LOG_LEVEL` env var (default: 'info')
///
/// # Example
/// ```typescript
/// mcpLog.info('Server started');
/// mcpLog.error({ err }, 'Fatal error');
/// ```
export const mcpLog = pino({
  transport: {
    target: 'pino/file',
    options: { destination: 2 },
  },
  level: process.env.UZ_MCP_LOG_LEVEL || 'info',
});
