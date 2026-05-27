from openai import OpenAI

client = OpenAI(
  base_url = "https://integrate.api.nvidia.com/v1",
  api_key = "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"
)

print("Sending completion request...")
completion = client.chat.completions.create(
  model="deepseek-ai/deepseek-v4-flash",
  messages=[{"role":"user","content":"Xin chào."}],
  temperature=1,
  top_p=0.95,
  max_tokens=200,
  extra_body={"chat_template_kwargs":{"thinking":True,"reasoning_effort":"high"}},
  stream=True
)

print("Connected! Reading stream chunks:")
for chunk in completion:
  if not getattr(chunk, "choices", None):
    continue
  reasoning = getattr(chunk.choices[0].delta, "reasoning", None) or getattr(chunk.choices[0].delta, "reasoning_content", None)
  if reasoning:
    print(reasoning, end="")
  if chunk.choices and chunk.choices[0].delta.content is not None:
    print(chunk.choices[0].delta.content, end="")
print("\nDone!")
