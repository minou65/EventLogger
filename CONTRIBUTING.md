# Contributing Guidelines

## Coding Standards

### Language and Documentation
- **All code comments, documentation, and text strings must be written in English**
- Variable names, function names, and class members should follow English naming conventions
- Exception: User-facing strings may be localized as needed

### Code Style
- C++14 standard
- Clear, descriptive naming conventions
- Consistent formatting throughout the codebase

### Naming Conventions
- **Local variables**: Use underscore suffix (e.g., `localVar_`, `counter_`, `buffer_`)
- **Parameter variables**: No suffix (e.g., `message`, `timestamp`, `value`)
- **Class member variables**: Use underscore prefix (e.g., `_head`, `_count`, `_liveCallback`)

## Project Settings

### EventLogger Configuration
- `EVENT_BUFFER_SIZE`: Configurable buffer size (default: 100)
  - Can be overridden by defining it before including EventLogger.h

## Best Practices
- Write self-documenting code with clear variable and function names
- Add comments for complex logic or non-obvious implementations
- Keep functions focused and single-purpose
- Follow existing code patterns in the project