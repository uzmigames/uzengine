import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { mcpLog } from './logger.js';

const server = new McpServer({
    name: 'uzengine-mcp',
    version: '0.2.0',
});

async function main(): Promise<void> {
    mcpLog.info('UzEngine MCP server starting');
    const transport = new StdioServerTransport();
    await server.connect(transport);
    mcpLog.info('UzEngine MCP server connected');
}

main().catch((err) => {
    mcpLog.fatal({ err }, 'MCP server fatal error');
    process.exit(1);
});
