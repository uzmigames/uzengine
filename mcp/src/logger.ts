import pino from 'pino';

// NEVER use console.log() in MCP server — it corrupts stdio transport
// All logging goes to stderr (fd 2)
export const mcpLog = pino({
  transport: {
    target: 'pino/file',
    options: { destination: 2 },
  },
  level: process.env.UZ_MCP_LOG_LEVEL || 'info',
});
