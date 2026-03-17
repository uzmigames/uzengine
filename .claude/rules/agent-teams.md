# MANDATORY: Use Teams for Multiple Agents

## Rule

When spawning 2 or more agents, you MUST use the Teams system (TeamCreate + named agents with team_name parameter). NEVER spawn multiple agents without a team.

## Why

Agents without teams:
- Edit the same files simultaneously causing conflicts
- Cannot communicate with each other via SendMessage
- Cannot be coordinated (no task assignment, no shutdown management)
- Break builds when one agent's changes conflict with another's
- Cannot be tracked or managed as a group

## How

```
// WRONG: Multiple agents without team
Agent(name="agent1", prompt="...")  // No team!
Agent(name="agent2", prompt="...")  // No team! → conflicts!

// CORRECT: Create team first, then agents
TeamCreate(team_name="my-task")
Agent(name="agent1", team_name="my-task", prompt="...")
Agent(name="agent2", team_name="my-task", prompt="...")
```

## Exceptions

- Single agent for a quick task: OK without team
- Agent with `isolation: "worktree"`: OK without team (isolated copy)

## When Assigning Work to Team Agents

- Each agent MUST work on DIFFERENT files — no two agents editing the same file
- Clearly specify which files each agent owns in their prompt
- Use SendMessage to coordinate when agents need to touch shared files
