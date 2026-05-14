# Enable Bedrock integration
export CLAUDE_CODE_USE_BEDROCK=1
export AWS_REGION=eu-central-1

# Optional: Override the region for the small/fast model (Haiku).
# Also applies to Bedrock Mantle.
export ANTHROPIC_SMALL_FAST_MODEL_AWS_REGION=eu-central-1

# Optional: Override the Bedrock endpoint URL for custom endpoints or gateways
# export ANTHROPIC_BEDROCK_BASE_URL=https://bedrock-runtime.us-east-1.amazonaws.com

claude --model eu.minimax.minimax-m2.5