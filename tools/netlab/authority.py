"""Small executable model of the server's vehicle authority contract."""
from dataclasses import dataclass


@dataclass
class AuthorityState:
    owner: str | None = None
    epoch: int = 0


class AuthorityModel:
    """One simulator at a time; epochs order movement across handoffs."""

    def __init__(self, owner=None):
        self.state = AuthorityState(owner=owner)

    def transfer(self, new_owner):
        if self.state.owner != new_owner:
            self.state.epoch += 1
            self.state.owner = new_owner
        return self.state

    def accepts_movement(self, sender, epoch):
        return sender == self.state.owner and epoch == self.state.epoch
